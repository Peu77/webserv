//
// Created by Emil Ebert on 01.05.25.
//

#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H


#include <string>
#include <unordered_map>
#include <sstream>
#include <server/buffer/SmartBuffer.h>
#include <memory>
#include <vector>

class HttpResponse {
private:
    int statusCode;
    std::string statusMessage;
    std::unordered_map<std::string, std::string> headers;
    std::shared_ptr<SmartBuffer> body;
    std::vector<std::string> setCookies;
    bool chunkedEncoding;;

public:
    // only used for chunked encoding, because there we have to send the header and body separately
    bool alreadySendHeader = false;

    static std::string getStatusMessage(int code);

    enum StatusCode {
        OK = 200,
        CREATED = 201,
        NO_CONTENT = 204,
        MOVED_PERMANENTLY = 301,
        FOUND = 302,
        BAD_REQUEST = 400,
        FORBIDDEN = 403,
        NOT_FOUND = 404,
        REQUEST_TIMEOUT = 408,
        CONFLICT = 409,
        CONTENT_TOO_LARGE = 413,
        REQUEST_URI_TOO_LONG = 414,
        UNSUPPORTED_MEDIA_TYPE = 415,
        METHOD_NOT_ALLOWED = 405,
        INTERNAL_SERVER_ERROR = 500,
        NOT_IMPLEMENTED = 501,
        GATEWAY_TIMEOUT = 504,
        HTTP_VERSION_NOT_SUPPORTED = 505,
    };

    HttpResponse() = delete;

    HttpResponse(int statusCode = StatusCode::OK);

    static HttpResponse html(StatusCode statusCode, const std::string &bodyMessage = "");

    void setStatus(int code, const std::string &message = "");

    [[nodiscard]] int getStatus() const;

    void setHeader(const std::string &name, const std::string &value);

    void setBody(const std::string &body);

    // this is only used for sending files
    void enableChunkedEncoding(std::shared_ptr<SmartBuffer> body);

    [[nodiscard]] std::string toString() const;

    [[nodiscard]] std::string toHeaderString() const;

    [[nodiscard]] std::shared_ptr<SmartBuffer> getBody() const;

    [[nodiscard]] bool isChunkedEncoding() const;

    [[nodiscard]] std::unordered_map<std::string, std::string> getHeaders() const;

    [[nodiscard]] bool hasHeader(const std::string &name) const;

    [[nodiscard]] std::string getHeader(const std::string &name) const;

    void addSetCookie(const std::string &cookie);

private:
    static void createNotFoundPage(std::stringstream &ss);
};


#endif //HTTPRESPONSE_H
