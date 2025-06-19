#include "SmartBuffer.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <algorithm>
#include <common/Logger.h>
#include <sys/stat.h>
#include <filesystem>
#include <vector>

#include "../FdHandler.h"
#include "webserv.h"

size_t SmartBuffer::tmpFileCount = 0;

SmartBuffer::SmartBuffer(const size_t maxMemorySize)
    : maxMemorySize(maxMemorySize) {
}

SmartBuffer::SmartBuffer(const int fd): fd(fd) {
    struct stat fileStat{};
    if (fstat(fd, &fileStat) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to get file size: " + std::to_string(fd));
        return;
    }
    size = fileStat.st_size;
    isFile = true;
    FdHandler::addFd(fd, POLLIN | POLLOUT, [this](const int fd, const short events) {
        return this->onFileEvent(fd, events);
    });
    fdCallbackRegistered = true;
}

SmartBuffer::~SmartBuffer() {
    unregisterCallback();
    if (isFile && fd >= 0) {
        Logger::log(LogLevel::DEBUG, "Closing file descriptor: " + std::to_string(fd));
        if (close(fd) < 0)
            Logger::log(LogLevel::ERROR,
                        "Failed to close file descriptor: " + std::to_string(fd));
        fd = -1;
    }

    if (!tmpFileName.empty() && std::filesystem::exists(tmpFileName)) {
        try {
            Logger::log(LogLevel::DEBUG, "Removing temporary file: " + tmpFileName);
            std::filesystem::remove(tmpFileName);
        } catch (const std::filesystem::filesystem_error &e) {
            Logger::log(LogLevel::ERROR, "Failed to remove temporary file: " + tmpFileName + ": " + e.what());
        }
    }
}

void SmartBuffer::unregisterCallback() {
    if (fdCallbackRegistered) {
        FdHandler::removeFd(fd);
        fdCallbackRegistered = false;
    }
}

bool SmartBuffer::onFileEvent(const int fd, const short events) {
    if (events & POLLOUT && !writeBuffer.empty()) {
        const ssize_t bytesWritten = ::write(fd, writeBuffer.data(), writeBuffer.length());
        if (bytesWritten <= 0) {
            close(fd);
            this->fd = -1;
            Logger::log(LogLevel::ERROR, "Failed to write to file: " + std::to_string(fd));
            return true;
        }
        size += bytesWritten;
        writeBuffer.erase(0, bytesWritten);
    }
    if (events & POLLIN && toRead > 0) {
        if (lseek(fd, readPos, SEEK_SET) < 0) {
            Logger::log(LogLevel::ERROR, "Failed to seek in file: " + std::to_string(fd));
            return false;
        }
        toRead = std::min(toRead, static_cast<size_t>(60000));
        std::vector<char> buf(toRead + 1);
        const ssize_t bytesRead = ::read(fd, buf.data(), toRead);
        if (bytesRead <= 0) {
            close(fd);
            this->fd = -1;
            return true;
        }

        buf[bytesRead] = '\0';
        readBuffer.append(buf.data(), bytesRead);
        readPos += bytesRead;
        toRead -= bytesRead;
    }
    return false;
}


void SmartBuffer::switchToFile() {
    if (isFile)
        return;

    size = 0;
    Logger::log(LogLevel::DEBUG, "Switching SmartBuffer to file mode");

    tmpFileName = TEMP_DIR_NAME "/smartbuffer_" + std::to_string(tmpFileCount++);
    fd = open(tmpFileName.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create temporary file: " + tmpFileName);
        return;
    }
    Logger::log(LogLevel::DEBUG, "Created temporary file: " + tmpFileName);

    writeBuffer.append(buffer);
    buffer.clear();

    isFile = true;
    FdHandler::addFd(fd, POLLIN | POLLOUT, [this](const int fd, const short events) {
        return this->onFileEvent(fd, events);
    });
    fdCallbackRegistered = true;
}


void SmartBuffer::append(const char *data, const size_t length) {
    if (!data || length == 0)
        return;

    if (isFile && fd >= 0)
        writeBuffer.append(data, length);
    else {
        buffer.append(data, length);
        size += length;
    }

    if (size > maxMemorySize)
        switchToFile();
}

void SmartBuffer::read(const size_t length) {
    if (length == 0 || size == 0)
        return;

    if (isFile && fd >= 0) {
        toRead += length;
        return;
    }

    if (readPos >= size)
        return;

    const size_t available = size - readPos;
    const size_t toRead = std::min(length, available);

    readBuffer.append(buffer.substr(readPos, toRead));
    readPos = std::min(readPos + toRead, size);
}

void SmartBuffer::cleanReadBuffer(size_t length) {
    if (length > readBuffer.length())
        length = readBuffer.length();

    readBuffer.erase(0, length);
}
