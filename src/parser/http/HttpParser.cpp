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

    std::istringstream iss(line);
    std::string methodStr, uri, version;


    if (!(iss >> methodStr >> uri >> version)) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP request line format");
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
        (version[7] != '0' && version[7] != '1')) {
        Logger::log(LogLevel::ERROR, "Invalid HTTP version: " + version);
        state = ParseState::ERROR;
        return false;
    }

    request->method = method.value();
    request->uri = uri;
    request->version = version;

    state = ParseState::HEADERS;
    headerStart = std::time(nullptr);
    return true;
}

bool HttpParser::parseHeaders() {
    size_t max_header_count;
    size_t client_max_header_size;
    size_t headerCount = 0;

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

            std::string transferEncoding = request->getHeader("Transfer-Encoding");
            chunkedTransfer = (transferEncoding == "chunked");

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

        if (++headerCount > max_header_count) {
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
        std::string value = line.substr(colonPos + 1);

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

        if (!name.empty())
            request->headers[name] = value;
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
    const bool isBodyComplete = appendToBody(buffer);
    buffer.clear();
    if (isBodyComplete)
        state = ParseState::COMPLETE;

    return isBodyComplete;
}

bool HttpParser::parseChunkedBody() {
    size_t client_max_body_size = clientConnection->config.client_max_body_size;
    while (true) {
        if (!hasChunkSize) {
            const size_t sizeEndPos = buffer.find("\r\n");
            if (sizeEndPos == std::string::npos)
                return false; // Need more data

            std::string sizeHex = buffer.substr(0, sizeEndPos);

            size_t semicolonPos = sizeHex.find(';');
            if (semicolonPos != std::string::npos) {
                sizeHex = sizeHex.substr(0, semicolonPos);
            }


            try {
                chunkSize = std::stoul(sizeHex, nullptr, 16);
                hasChunkSize = true;
            } catch (...) {
                Logger::log(LogLevel::ERROR, "Invalid chunk size format");
                std::cout << "Error: " << sizeHex << std::endl;
                state = ParseState::ERROR;
                return false;
            }

            buffer.erase(0, sizeEndPos + 2);
        }


        if (chunkSize == 0) {
            if (buffer.length() < 2)
                return false;

            if (buffer.substr(0, 2) != "\r\n") {
                Logger::log(LogLevel::ERROR, "Missing CRLF after final chunk");
                state = ParseState::ERROR;
                return false;
            }

            buffer.erase(0, 2);
            state = ParseState::COMPLETE;
            return true;
        }

        if (buffer.length() < chunkSize + 2)
            return false;

        if (client_max_body_size > 0 &&
            request->totalBodySize + chunkSize > client_max_body_size) {
            Logger::log(LogLevel::ERROR, "Chunked body exceeds maximum allowed size of " +
                                         std::to_string(client_max_body_size));
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
