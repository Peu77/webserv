#include "RequestHandler.h"

#include <sys/stat.h>
#include <fstream>
#include <regex>
#include <filesystem>
#include <common/Logger.h>
#include <server/ClientConnection.h>
#include <server/FdHandler.h>
#include <unistd.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <common/SessionManager.h>
#include <server/handler/CallbackHandler.h>

std::optional<HttpResponse> RequestHandler::handlePost() {
    if (!std::filesystem::exists(routePath)) {
        return HttpResponse::html(HttpResponse::StatusCode::NOT_FOUND,
                                  "Target directory does not exist");
    }

    if (isFile) {
        return HttpResponse::html(HttpResponse::StatusCode::BAD_REQUEST,
                                  "Target must be a directory for file uploads");
    }

    if (request->totalBodySize == 0)
        return HttpResponse::html(HttpResponse::StatusCode::NO_CONTENT,
                                  "Empty request body");

    const std::string contentType = request->getHeader("Content-Type");

    if (contentType.find("multipart/form-data") != std::string::npos)
        return handlePostMultipart(contentType);

    if (contentType.find("test/file") != std::string::npos)
        return handlePostTestFile();

    return HttpResponse::html(HttpResponse::StatusCode::UNSUPPORTED_MEDIA_TYPE,
                              "Invalid Content-Type");
}

std::optional<HttpResponse> RequestHandler::handlePostTestFile() {
    if (request->totalBodySize > 100)
        return HttpResponse::html(HttpResponse::StatusCode::CONTENT_TOO_LARGE);
    const std::string filename = "test_file_" + std::to_string(std::time(nullptr)) + ".txt";
    const std::filesystem::path fullPath = std::filesystem::path(routePath) / filename;

    fileWriteFd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fileWriteFd == -1) {
        return HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR,
                                  "Could not open file for writing");
    }

    const std::string absolutePath = absolute(fullPath).lexically_normal().string();
    client->sessionId = SessionManager::getSessionId(request->getHeader("Cookie"), client->isNewSession);
    SessionManager::addUploadedFile(client->sessionId, absolutePath);
    FdHandler::addFd(fileWriteFd,POLLOUT, [this, filename](const int fd, const short events) {
        (void) events;
        request->body->read(60000);

        const std::string readBuffer = request->body->getReadBuffer();
        if (!readBuffer.empty()) {
            const ssize_t writen = write(fd, readBuffer.data(), readBuffer.length());

            if (writen <= 0) {
                Logger::log(LogLevel::ERROR, "Failed to write to file: " + std::to_string(fd));
                setResponse(HttpResponse::html(HttpResponse::INTERNAL_SERVER_ERROR,
                                                       "Failed to write to file"));
                close(fileWriteFd);
                fileWriteFd = -1;
                return true;
            }
            request->body->cleanReadBuffer(writen);
        }

        if (request->body->getReadPos() >= request->body->getSize()) {
            setResponse(HttpResponse::html(HttpResponse::StatusCode::CREATED,
                                                   "201 Created: " + filename + " file uploaded successfully"));
            close(fileWriteFd);
            fileWriteFd = -1;
            return true;
        }
        return false;
    });
    return std::nullopt;
}

std::optional<HttpResponse> RequestHandler::handlePostMultipart(const std::string &contentType) {
    std::smatch match;
    std::regex boundaryRegex("boundary\\s*=\\s*([^;]+)");
    if (!std::regex_search(contentType, match, boundaryRegex)) {
        return HttpResponse::html(HttpResponse::StatusCode::BAD_REQUEST, "Missing boundary");
    }

    std::string boundary = "--" + match[1].str();
    std::string endBoundary = boundary + "--";

    auto state = std::make_shared<MultipartParseState>();
    state->boundary = boundary;
    state->endBoundary = endBoundary;
    state->uploadedFiles = 0;
    state->fileNames = std::vector<std::string>();
    state->currentState = MultipartParseStateEnum::LOOKING_FOR_BOUNDARY;

    state->parseBuffer = "";

    this->postRequestCallbackId = CallbackHandler::registerCallback([this, state]() {
        // TODO: fix magic number 60000
        request->body->read(60000);
        std::string chunk = request->body->getReadBuffer();

        if (chunk.empty() && request->body->getReadPos() >= request->body->getSize()) {
            if (state->fileWriteFd >= 0) {
                close(state->fileWriteFd);
                state->fileWriteFd = -1;
            }

            if (state->uploadedFiles == 0) {
                setResponse(HttpResponse::html(HttpResponse::StatusCode::BAD_REQUEST,
                                                       "No files found in request"));
            } else {
                setResponse(HttpResponse::html(HttpResponse::StatusCode::CREATED,
                                                       "201 Created: " + std::to_string(state->uploadedFiles) +
                                                       " file(s) uploaded successfully"));
            }

            return true;
        }

        state->parseBuffer.append(chunk);
        request->body->cleanReadBuffer(chunk.length());

        processMultipartBuffer(state);

        return false;
    });

    return std::nullopt;
}

void RequestHandler::processMultipartBuffer(std::shared_ptr<MultipartParseState> state) {
    while (!state->parseBuffer.empty()) {
        switch (state->currentState) {
            case MultipartParseStateEnum::LOOKING_FOR_BOUNDARY: {
                size_t boundaryPos = state->parseBuffer.find(state->boundary);
                if (boundaryPos == std::string::npos) {
                    if (state->parseBuffer.size() > state->boundary.size()) {
                        state->parseBuffer = state->parseBuffer.substr(
                            state->parseBuffer.size() - state->boundary.size());
                    }
                    return;
                }

                if (boundaryPos + state->endBoundary.size() <= state->parseBuffer.size() &&
                    state->parseBuffer.substr(boundaryPos, state->endBoundary.size()) == state->endBoundary) {
                    state->parseBuffer.clear();
                    return;
                }

                state->parseBuffer.erase(0, boundaryPos + state->boundary.size());

                if (state->parseBuffer.size() >= 2 && state->parseBuffer.substr(0, 2) == "\r\n") {
                    state->parseBuffer.erase(0, 2);
                }

                state->currentState = MultipartParseStateEnum::READING_HEADERS;
                break;
            }

            case MultipartParseStateEnum::READING_HEADERS: {
                size_t headersEnd = state->parseBuffer.find("\r\n\r\n");
                if (headersEnd == std::string::npos) {
                    return;
                }

                std::string headers = state->parseBuffer.substr(0, headersEnd);
                state->parseBuffer.erase(0, headersEnd + 4);

                std::smatch match;
                std::regex filenameRegex("filename\\s*=\\s*\"([^\"]*)\"");
                if (!std::regex_search(headers, match, filenameRegex) || match[1].str().empty()) {
                    state->currentState = MultipartParseStateEnum::LOOKING_FOR_BOUNDARY;
                    break;
                }

                std::string filename = match[1].str();
                std::filesystem::path fullPath = std::filesystem::path(routePath) / filename;
                state->fileNames.push_back(filename);

                state->fileWriteFd = open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

                std::string absolutePath = absolute(fullPath).lexically_normal().string();
                client->sessionId = SessionManager::getSessionId(request->getHeader("Cookie"), client->isNewSession);
                SessionManager::addUploadedFile(client->sessionId, absolutePath);
                if (state->fileWriteFd == -1) {
                    Logger::log(LogLevel::ERROR, "Failed to open file for writing: " + filename);
                    state->currentState = MultipartParseStateEnum::LOOKING_FOR_BOUNDARY;
                    break;
                }

                state->currentState = MultipartParseStateEnum::READING_FILE_CONTENT;
                break;
            }

            case MultipartParseStateEnum::READING_FILE_CONTENT: {
                size_t nextBoundaryPos = state->parseBuffer.find(state->boundary);
                if (nextBoundaryPos == std::string::npos) {
                    size_t safeLength = state->parseBuffer.size() - state->boundary.size();
                    if (safeLength > 0) {
                        if (write(state->fileWriteFd, state->parseBuffer.data(), safeLength) <= 0) {
                            Logger::log(LogLevel::ERROR, "Failed to write to file: " +
                                                         std::to_string(state->fileWriteFd));
                        }
                        state->parseBuffer.erase(0, safeLength);
                    }
                    return;
                }

                size_t contentEnd = nextBoundaryPos;
                if (contentEnd >= 2 && state->parseBuffer.substr(contentEnd - 2, 2) == "\r\n") {
                    contentEnd -= 2;
                }

                if (contentEnd > 0) {
                    pollfd pfd{};
                    pfd.fd = state->fileWriteFd;
                    pfd.events = POLLOUT;
                    pfd.revents = 0;

                    poll(&pfd, 1, 0);
                    if (pfd.revents & POLLOUT) {
                        if (write(state->fileWriteFd, state->parseBuffer.data(), contentEnd) <= 0) {
                            Logger::log(LogLevel::ERROR, "Failed to write to file: " +
                                                         std::to_string(state->fileWriteFd));
                        }
                    } else
                        Logger::log(LogLevel::ERROR, "File descriptor not ready for writing: " +
                                                     std::to_string(state->fileWriteFd));
                }

                close(state->fileWriteFd);
                state->fileWriteFd = -1;
                state->uploadedFiles++;

                state->parseBuffer.erase(0, nextBoundaryPos);
                state->currentState = MultipartParseStateEnum::LOOKING_FOR_BOUNDARY;
                break;
            }
        }
    }
}
