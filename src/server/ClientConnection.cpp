//
// Created by Emil Ebert on 01.05.25.
//

#include "ClientConnection.h"

#include <unistd.h>
#include <webserv.h>
#include <common/Logger.h>

#include "FdHandler.h"
#include "requestHandler/RequestHandler.h"
#include <execinfo.h>
#include <iostream>
#include <cstdlib>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <common/SessionManager.h>

#include "ServerPool.h"
#include "handler/MetricHandler.h"
#include <sys/socket.h>
#include <netinet/in.h>


ClientConnection::ClientConnection(const int clientFd,
                                   const sockaddr_in clientAddr,
                                   const Server *connectedServer): fd(clientFd),
                                                                   clientAddr(clientAddr), parser(this),
                                                                   connectedServer(connectedServer) {
    config.client_body_timeout = 0;
    config.keepalive_timeout = 0;
    config.keepalive_requests = 0;
    config.headerConfig.client_header_timeout = ServerPool::getHttpConfig().headerConfig.client_header_timeout;
    config.headerConfig.client_max_header_size = ServerPool::getHttpConfig().headerConfig.client_max_header_size;
    config.headerConfig.client_max_header_count = ServerPool::getHttpConfig().headerConfig.client_max_header_count;
    config.client_max_body_size = 1 * 1024 * 1024; // 1 MB

#if defined(__APPLE__)
    int opt = 1;
    setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif


    FdHandler::addFd(clientFd, POLLIN | POLLOUT, [this](const int fd, const short events) {
        (void) fd;
        if (shouldClose)
            return true;
        if (events & POLLIN && !hasPendingResponse())
            this->handleInput();
        if (events & POLLOUT)
            this->handleOutput();
        return false;
    });
    MetricHandler::incrementMetric("new_connections", 1);
}

ClientConnection::~ClientConnection() {
    FdHandler::removeFd(this->fd);
    this->clearResponse();
    parser.reset();
    if (fd != -1) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        Logger::log(LogLevel::DEBUG, "closed client fd: " + std::to_string(fd));
        fd = -1;
    }
    delete requestHandler;
    requestHandler = nullptr;
    MetricHandler::incrementMetric("disconnects", 1);
}

void ClientConnection::handleInput() {
    // TODO: fix magic number
    char buffer[60001];
    const ssize_t bytesRead = read(fd, buffer, 60000);
    if (bytesRead < 0) {
        Logger::log(LogLevel::ERROR, "Failed to read from client fd: " + std::to_string(fd));
        shouldClose = true;
        return;
    }

    if (bytesRead == 0) {
        shouldClose = true;
        return;
    }

    buffer[bytesRead] = '\0';

    // so it doasn't timeout while reading the request
    lastPackageSend = 0;
    MetricHandler::incrementMetric("bytes_received", bytesRead);

    if (parser.parse(buffer, bytesRead)) {
        const auto request = parser.getRequest();
        keepAlive = request->getHeader("Connection") == "keep-alive";
        request->printRequest();

        Logger::log(LogLevel::DEBUG, "Request Parsed");
        MetricHandler::incrementMetric("requests", 1);

        try {
            delete requestHandler;
            requestHandler = new RequestHandler(this, request, config);
            requestHandler->execute();
        } catch (std::exception &e) {
            Logger::log(LogLevel::ERROR, "Error handling request: " + std::string(e.what()));
            const HttpResponse response = HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR);
            setResponse(RequestHandler::handleCustomErrorPage(response, config, std::nullopt));
        }

        parser.reset();
        debugBuffer.clear();
        return;
    }

    if (parser.hasError()) {
        const HttpResponse response = HttpResponse::html(parser.getErrorCode());
        setResponse(RequestHandler::handleCustomErrorPage(response, config, std::nullopt));
        parser.reset();
        debugBuffer.clear();
    }
}

void ClientConnection::handleOutput() {
    if (hasPendingResponse()) {
        lastPackageSend = 0;
        HttpResponse &response = getResponse().value();
        if (response.getBody()->isStillWriting())
            return;
        if (keepAlive)
            response.setHeader("Connection", "keep-alive");
        else
            response.setHeader("Connection", "close");

        handleFileOutput();
    }
}

void ClientConnection::handleFileOutput() {
    if (!response.value().alreadySendHeader) {
        const std::string header = response.value().toHeaderString();
        Logger::log(LogLevel::DEBUG, "Sending response header: " + header);
        Logger::log(LogLevel::INFO, "status code: " + std::to_string(response->getStatus()));
        if (send(fd, header.c_str(), header.length(), MSG_NOSIGNAL) <= 0) {
            Logger::log(LogLevel::ERROR, "Failed to write header to client");
            clearResponse();
            return;
        }
        MetricHandler::incrementMetric("bytes_send", header.length());
        response.value().alreadySendHeader = true;
        return;
    }


    std::shared_ptr<SmartBuffer> body = response->getBody();
    // TODO: fix magic number number should probably be higher
    body->read(4000);

    const std::string readBuffer = body->getReadBuffer();

    if (!readBuffer.empty()) {
        std::stringstream chunkHeader;
        chunkHeader << std::hex << readBuffer.length() << "\r\n";
        const std::string chunkHeaderStr = chunkHeader.str();

        std::string chunk;
        chunk.append(chunkHeaderStr);
        chunk.append(readBuffer);
        chunk.append("\r\n");
        if (chunkHeaderStr.empty()) {
            Logger::log(LogLevel::ERROR, "Chunk header is empty, cannot send chunk");
            clearResponse();
            return;
        }

        ssize_t bytesSent = send(fd, chunk.c_str(), chunk.length(), MSG_NOSIGNAL);
        if (bytesSent != static_cast<ssize_t>(chunk.length())) {
            Logger::log(LogLevel::ERROR, "Failed to write chunk header to client");
            clearResponse();
            return;
        }

        body->cleanReadBuffer(bytesSent - chunkHeaderStr.length() - 2);
        MetricHandler::incrementMetric("bytes_send", bytesSent);
        return;
    }


    if (body->getReadPos() >= body->getSize()) {
        if (send(fd, "0\r\n\r\n", 5, MSG_NOSIGNAL) <= 0)
            Logger::log(LogLevel::ERROR, "Failed to write final chunk to client");
        lastPackageSend = std::time(nullptr);
        MetricHandler::incrementMetric("responses", 1);
        Logger::log(LogLevel::INFO, "Client response sent");
        clearResponse();
    }
}


void ClientConnection::clearResponse() {
    cgiProcessStart = 0;
    response.reset();
    if (!keepAlive || requestCount > config.
        keepalive_requests) {
        shouldClose = true;
    }

    delete requestHandler;
    requestHandler = nullptr;
}

void ClientConnection::setResponse(HttpResponse response) {
    if (isNewSession) {
        const std::string cookie = " sessionId=" + sessionId + "; Path=/; HttpOnly";
        response.addSetCookie(cookie);
    }

    cgiProcessStart = 0;
    this->response = response;
    parser.reset();
    requestCount++;
}

void ClientConnection::setConfig(const ServerConfig &config) {
    this->config = config;
}
