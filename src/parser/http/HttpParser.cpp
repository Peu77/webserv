//
// Created by Emil Ebert on 01.05.25.
//

#include "HttpParser.h"

#include "HttpParser.h"
#include "common/Logger.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <unistd.h>
#include <webserv.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <server/ServerPool.h>

ssize_t HttpParser::tmpFileCount = 0;

HttpParser::HttpParser(ClientConnection *clientConnection)
    : state(ParseState::REQUEST_LINE),
      request(std::make_shared<HttpRequest>()),
      contentLength(0),
      chunkedTransfer(false),
      clientConnection(clientConnection) {
}

HttpParser::~HttpParser() {
    reset();
}

std::string HttpParser::decodeString(const std::string &input) {
    std::string decoded;
    decoded.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            int value;
            std::istringstream iss(input.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += input[i];
            }
        } else if (input[i] == '+') {
            decoded += ' ';
        } else {
            decoded += input[i];
        }
    }
    return decoded;
}

bool HttpParser::parse(const char *data, const size_t length) {
    if (state == ParseState::COMPLETE || state == ParseState::ERROR)
        return false;

    buffer.append(data, length);

    bool needMoreData = false;

    while (!needMoreData) {
        switch (state) {
            case ParseState::REQUEST_LINE:
                needMoreData = !parseRequestLine();
                break;

            case ParseState::HEADERS:
                needMoreData = !parseHeaders();
                break;

            case ParseState::BODY:
                needMoreData = !parseBody();
                break;

            case ParseState::COMPLETE:
                return true;

            case ParseState::ERROR:
                return false;
        }
    }

    return false;
}

bool HttpParser::parseRequestLine() {
    size_t endPos = buffer.find("\r\n");
    if (endPos == std::string::npos)
        return false;

    if (endPos > ServerPool::getHttpConfig().max_request_line_size) {
        Logger::log(LogLevel::ERROR, "Request line too long");
        state = ParseState::ERROR;
        errorCode = HttpResponse::StatusCode::REQUEST_URI_TOO_LONG;
        return false;
    }

    std::string line = buffer.substr(0, endPos);
    buffer.erase(0, endPos + 2);

    if (line.empty() || std::isspace(line[0])) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP request line: " + line);
        state = ParseState::ERROR;
        errorCode = HttpResponse::StatusCode::BAD_REQUEST;
        return false;
    }

    std::istringstream iss(line);
    std::string methodStr, uri, version;


    if (!(iss >> methodStr >> uri >> version)) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP request line format");
        state = ParseState::ERROR;
        return false;
    }

    if (iss.rdbuf()->in_avail() > 0) {
        Logger::log(LogLevel::ERROR, "Extra data found in request line: " + line);
        state = ParseState::ERROR;
        return false;
    }


    auto method = stringToMethod(methodStr);
    if (method == std::nullopt) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP method: " + methodStr);
        state = ParseState::ERROR;
        return false;
    }

    if ((uri.empty() || uri[0] != '/') && uri.find("://") == std::string::npos) {
        Logger::log(LogLevel::ERROR, "Invalid URI format: " + uri);
        state = ParseState::ERROR;
        return false;
    }

    if (version.length() < 8 || version.substr(0, 5) != "HTTP/" ||
        version[5] != '1' || version[6] != '.' ||
        (version[7] != '1')) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP version: " + version);
        errorCode = HttpResponse::StatusCode::HTTP_VERSION_NOT_SUPPORTED;
        state = ParseState::ERROR;
        return false;
    }

    request->method = method.value();
    request->uri = decodeString(uri);
    request->version = version;

    state = ParseState::HEADERS;
    headerStart = std::time(nullptr);
    return true;
}

bool HttpParser::parseHeaders() {
    size_t max_header_count;
    size_t client_max_header_size;

    while (true) {
        max_header_count = clientConnection->config.headerConfig.client_max_header_count;
        client_max_header_size = clientConnection->config.headerConfig.client_max_header_size;

        const size_t endPos = buffer.find("\r\n");
        if (endPos == std::string::npos)
            return false;

        if (endPos == 0) {
            headerStart = 0;
            buffer.erase(0, 2);

            std::string contentLengthStr = request->getHeader("Content-Length");
            if (!contentLengthStr.empty()) {
                try {
                    if (!contentLengthStr.empty() && contentLengthStr[0] == '-') {
                        Logger::log(LogLevel::ERROR, "Content-Length cannot be negative");
                        state = ParseState::ERROR;
                        return false;
                    }
                    contentLength = std::stoul(contentLengthStr);
                } catch (...) {
                    state = ParseState::ERROR;
                    return false;
                }
            }

            if (request->headers.find("Host") == request->headers.end()) {
                Logger::log(LogLevel::ERROR, "Host header is missing");
                state = ParseState::ERROR;
                errorCode = HttpResponse::StatusCode::BAD_REQUEST;
                return false;
            }

            if (!contentLengthStr.empty() && chunkedTransfer) {
                errorCode = HttpResponse::StatusCode::BAD_REQUEST;
                state = ParseState::ERROR;
                Logger::log(LogLevel::ERROR, "Content-Length and Transfer-Encoding cannot be used together");
                return false;
            }

            if (contentLength > 0 || chunkedTransfer) {
                bodyStart = std::time(nullptr);
                state = ParseState::BODY;
            } else {
                state = ParseState::COMPLETE;
            }
            return true;
        }

        if (client_max_header_size > 0 && endPos > client_max_header_size) {
            Logger::log(LogLevel::ERROR, "Headers exceed maximum allowed size");
            state = ParseState::ERROR;
            return false;
        }

        if (++request->headerCount > max_header_count) {
            Logger::log(LogLevel::ERROR, "Too many headers in request");
            state = ParseState::ERROR;
            return false;
        }

        std::string line = buffer.substr(0, endPos);
        buffer.erase(0, endPos + 2);

        static const std::regex headerRegex(R"(^[!#$%&'*+\-.^_`|~0-9A-Za-z]+:[ \t]*[^\r\n]*$)");
        if (!std::regex_match(line, headerRegex)) {
            Logger::log(LogLevel::ERROR, "Invalid header format: " + line);
            state = ParseState::ERROR;
            return false;
        }

        size_t colonPos = line.find(':');
        std::string name = line.substr(0, colonPos);
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (!name.empty()) name[0] = std::toupper(name[0]);
        for (size_t i = 1; i < name.size(); ++i) {
            if (name[i - 1] == '-') name[i] = std::toupper(name[i]);
        }
        std::string value = line.substr(colonPos + 1);

        if (std::any_of(value.begin(), value.end(), [](char c) {
            return std::iscntrl(c) && c != '\r' && c != '\n';
        })) {
            Logger::log(LogLevel::ERROR, "Header value contains control characters: " + value);
            state = ParseState::ERROR;
            return false;
        }

        value.erase(0, value.find_first_not_of(" \t"));

        if (name == "Host") {
            if (request->headers.find("Host") != request->headers.end()) {
                Logger::log(LogLevel::ERROR, "Duplicate Host header");
                state = ParseState::ERROR;
                return false;
            }

            if (value.empty()) {
                Logger::log(LogLevel::ERROR, "Host header cannot be empty");
                state = ParseState::ERROR;
                return false;
            }

            ServerPool::matchVirtualServer(clientConnection, value);
        }

        if (name == "Transfer-Encoding") {
            if (value != "chunked") {
                Logger::log(LogLevel::ERROR, "Invalid Transfer-Encoding header: " + value);
                errorCode = HttpResponse::StatusCode::NOT_IMPLEMENTED;
                state = ParseState::ERROR;
                return false;
            }
            chunkedTransfer = true;
        }

        if (!name.empty()) {
            request->headers[name] = value;
        }
    }
}

// TODO: handle chunkedTransfer in a separate function so the code is cleaner
bool HttpParser::parseBody() {
    size_t client_max_body_size = clientConnection->config.client_max_body_size;
    if (!chunkedTransfer && client_max_body_size > 0 &&
        contentLength > client_max_body_size) {
        Logger::log(LogLevel::ERROR, "Content-Length exceeds maximum allowed body size");
        Logger::log(LogLevel::ERROR, "tried contentLength: " + std::to_string(contentLength) +
                                     " client_max_body_size: " + std::to_string(client_max_body_size));
        state = ParseState::ERROR;
        errorCode = HttpResponse::StatusCode::CONTENT_TOO_LARGE;
        return false;
    }

    if (chunkedTransfer) {
        return parseChunkedBody();
    }


    if (request->totalBodySize + buffer.length() > contentLength) {
        Logger::log(LogLevel::ERROR, "Body exceeds Content-Length");
        state = ParseState::ERROR;
        return false;
    }

    const bool isBodyComplete = appendToBody(buffer);
    buffer.clear();
    if (isBodyComplete)
        state = ParseState::COMPLETE;

    return isBodyComplete;
}

bool HttpParser::parseChunkedBody() {
    size_t client_max_body_size = clientConnection->config.client_max_body_size;
    while (true) {
        const size_t sizeEndPos = buffer.find("\r\n");
        if (sizeEndPos == std::string::npos)
            return false; // Need more data

        const std::string line = buffer.substr(0, sizeEndPos);

        if (!hasChunkSize) {
            std::string sizeHex = buffer.substr(0, sizeEndPos);

            size_t semicolonPos = sizeHex.find(';');
            if (semicolonPos != std::string::npos) {
                sizeHex = sizeHex.substr(0, semicolonPos);
            }

            if (!std::all_of(sizeHex.begin(), sizeHex.end(), [](const char c) {
                return std::isxdigit(c);
            })) {
                Logger::log(LogLevel::ERROR, "Invalid chunk size format: " + sizeHex);
                state = ParseState::ERROR;
                return false;
            }

            try {
                chunkSize = std::stoul(sizeHex, nullptr, 16);
                hasChunkSize = true;
            } catch (...) {
                Logger::log(LogLevel::ERROR, "Invalid chunk size format: " + sizeHex);
                state = ParseState::ERROR;
                return false;
            }

            if (client_max_body_size > 0 &&
                request->totalBodySize + chunkSize > client_max_body_size) {
                Logger::log(LogLevel::ERROR, "Chunked body exceeds maximum allowed size of " +
                                             std::to_string(client_max_body_size));
                state = ParseState::ERROR;
                return false;
            }

            buffer.erase(0, sizeEndPos + 2);
            continue;
        }


        if (chunkSize == 0) {
            if (!line.empty()) {
                Logger::log(LogLevel::ERROR, "Final chunk size is 0 but line is not empty: " + line);
                state = ParseState::ERROR;
                return false;
            }

            buffer.erase(0, 2);
            if (!buffer.empty()) {
                Logger::log(LogLevel::ERROR, "Extra data found after final chunk");
                state = ParseState::ERROR;
                return false;
            }
            state = ParseState::COMPLETE;
            return true;
        }

        if (buffer.length() < chunkSize + 2) {
            return false;
        }

        if (buffer[chunkSize] != '\r' || buffer[chunkSize + 1] != '\n') {
            Logger::log(LogLevel::ERROR, "Invalid chunked body format: missing CRLF after chunk data");
            state = ParseState::ERROR;
            return false;
        }

        std::string chunkedBody = buffer.substr(0, chunkSize);
        appendToBody(chunkedBody);
        hasChunkSize = false;
        buffer.erase(0, chunkSize + 2);
    }
}

bool HttpParser::appendToBody(const std::string &data) {
    size_t client_max_body_size = clientConnection->config.client_max_body_size;

    if (client_max_body_size > 0 &&
        request->totalBodySize > client_max_body_size) {
        Logger::log(LogLevel::ERROR, "Body exceeds maximum allowed size");
        state = ParseState::ERROR;
        errorCode = HttpResponse::StatusCode::CONTENT_TOO_LARGE;
        return false;
    }

    request->totalBodySize += data.length();
    request->body->append(data.data(), data.length());

    return request->totalBodySize >= contentLength;
}


std::optional<HttpMethod> HttpParser::stringToMethod(const std::string &method) {
    if (method == "GET") return std::make_optional(GET);
    if (method == "POST") return std::make_optional(POST);
    if (method == "PUT") return std::make_optional(PUT);
    if (method == "DELETE") return std::make_optional(DELETE);
    if (method == "HEAD") return std::make_optional(HEAD);
    if (method == "PATCH") return std::make_optional(PATCH);
    if (method == "OPTIONS") return std::make_optional(OPTIONS);

    return std::nullopt;
}

std::shared_ptr<HttpRequest> HttpParser::getRequest() const {
    return request;
}

void HttpParser::reset() {
    errorCode = HttpResponse::StatusCode::BAD_REQUEST;
    state = ParseState::REQUEST_LINE;
    request.reset();
    request = std::make_shared<HttpRequest>();
    buffer.clear();
    contentLength = 0;
    chunkedTransfer = false;
    headerStart = 0;
    bodyStart = 0;
    chunkSize = 0;
    hasChunkSize = false;
}

bool HttpParser::isHttpStatusCode(const int statusCode) {
    return (statusCode >= 100 && statusCode < 600);
}
