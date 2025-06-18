//
// Created by Emil Ebert on 01.05.25.
//

#include "HttpResponse.h"
#include <iostream>
#include <utility>
#include <parser/http/HttpParser.h>

#include "NotFoundImage.h"

HttpResponse::HttpResponse(const int statusCode)
    : statusCode(statusCode),
      chunkedEncoding(true) {
    body = std::make_shared<SmartBuffer>();
    statusMessage = getStatusMessage(statusCode);
    headers["Transfer-Encoding"] = "chunked";
}

void HttpResponse::setStatus(const int code, const std::string &message) {
    statusCode = code;
    statusMessage = message.empty() ? getStatusMessage(code) : message;
}

int HttpResponse::getStatus() const {
    return statusCode;
}

void HttpResponse::setHeader(const std::string &name, const std::string &value) {
    headers[name] = value;
}

void HttpResponse::setBody(const std::string &body) {
    this->body->append(body.c_str(), body.length());
    if (!chunkedEncoding) {
        headers["Content-Length"] = std::to_string(body.length());
    }
}

void HttpResponse::enableChunkedEncoding(std::shared_ptr<SmartBuffer> body) {
    this->body = std::move(body);
    chunkedEncoding = true;
    headers.erase("Content-Length");
}

bool HttpResponse::isChunkedEncoding() const {
    return chunkedEncoding;
}

std::string HttpResponse::toString() const {
    std::stringstream response;

    response << toHeaderString();

    response << body;

    return response.str();
}

std::string HttpResponse::toHeaderString() const {
    std::stringstream response;

    response << "HTTP/1.1 " << statusCode << " " << statusMessage << "\r\n";
    for (const auto &[fst, snd]: headers) {
        response << fst << ": " << snd << "\r\n";
    }

    for (const auto& cookie : setCookies)
        response << "Set-Cookie: " << cookie << "\r\n";

    response << "\r\n";
    return response.str();
}

std::string HttpResponse::getStatusMessage(const int code) {
    switch (code) {
        case OK: return "OK";
        case CREATED: return "Created";
        case NO_CONTENT: return "No Content";
        case MOVED_PERMANENTLY: return "Moved Permanently";
        case FOUND: return "Found";
        case BAD_REQUEST: return "Bad Request";
        case NOT_FOUND: return "Not Found";
        case REQUEST_TIMEOUT: return "Request Timeout";
        case CONTENT_TOO_LARGE: return "Content Too Large";
        case METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case REQUEST_URI_TOO_LONG: return "Request URI Too Long";
        case NOT_IMPLEMENTED: return "Not Implemented";
        case FORBIDDEN: return "Forbidden";
        case CONFLICT: return "Conflict";
        case UNSUPPORTED_MEDIA_TYPE: return "Unsupported Media Type";
        case GATEWAY_TIMEOUT: return "Gateway Timeout";
        case HTTP_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported";
        default: return "Unknown";

    }
}

HttpResponse HttpResponse::html(const StatusCode statusCode, const std::string &bodyMessage) {
    HttpResponse response(statusCode);
    std::stringstream ss;
    if (statusCode == NOT_FOUND)
        createNotFoundPage(ss);
    else
        ss << "<html>"
                "<head>"
                "<title>" << statusCode << "</title>"
                "</head>"
                "<body>"
                "<h1>" << statusCode << " " << response.statusMessage
                << (!bodyMessage.empty() ? (": " + bodyMessage) : "") << "</h1>"
                "</body>"
                "</html>";
    response.setBody(ss.str());
    response.setHeader("Content-Type", "text/html");
    return response;
}

void HttpResponse::createNotFoundPage(std::stringstream &ss) {
    ss << "<html>"
            "<head>"
            "<title>" << NOT_FOUND << " " << "not found" << "</title>"
            "<style>"
            "body { margin: 0; padding: 0; height: 100vh; }"
            "body { background: " << NOT_FOUND_IMG_URL << " no-repeat center center; background-size: cover; }"
            "body::before { content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 100%; "
            "background-color: rgba(0, 0, 0, 0.6); }"
            ".content { position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); "
            "text-align: center; z-index: 1; }"
            ".big-404 { font-size: 120px; font-weight: bold; color: white; margin: 0; }"
            "h1 { color: white; }"
            "</style>"
            "</head>"
            "<body>"
            "<div class=\"content\">"
            "<p class=\"big-404\">404</p>"
            "<h1>Page Not Found</h1>"
            "</div>"
            "</body>"
            "</html>";
}

std::shared_ptr<SmartBuffer> HttpResponse::getBody() const {
    return body;
}

std::unordered_map<std::string, std::string> HttpResponse::getHeaders() const {
    return headers;
}

bool HttpResponse::hasHeader(const std::string &name) const {
    return headers.find(name) != headers.end();
}

std::string HttpResponse::getHeader(const std::string &name) const {
    auto it = headers.find(name);
    if (it != headers.end()) {
        return it->second;
    }
    return "";
}

void HttpResponse::addSetCookie(const std::string &cookie) {
    setCookies.push_back(cookie);
}
