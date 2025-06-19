#include "SessionManager.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <iterator>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <poll.h>

std::unordered_map<std::string, std::vector<std::string> > SessionManager::sessions;
std::mutex SessionManager::mutex_;

// Generates a string that is unique (statistically), hard to guess, and suitable for use as a session token or cookie value
static std::string generateSessionId() {
    static std::random_device rd;                                   // Gathers entropy from hardware (if available), used to seed the RNG
    static std::mt19937_64 eng(rd());                               // 64-bit Mersenne Twister RNG seeded with rd
    static std::uniform_int_distribution<uint64_t> dist;
    uint64_t val = dist(eng);                                       // Uniformly distributes a random uint64_t value
    std::stringstream ss;                                           // Converts the random number into a fixed-length, zero-padded, hexadecimal string
    ss << std::hex << std::setw(16) << std::setfill('0') << val;
    return ss.str();                                                // e.g. "3a7f9b2e1c4d5e6f"
}

std::string SessionManager::getSessionId(const std::string &cookieHeader, bool &isNew) {
    std::lock_guard<std::mutex> lk(mutex_);
    isNew = false;
    // look for “sessionId=…”
    auto pos = cookieHeader.find("sessionId=");
    if (pos != std::string::npos) {
        pos += strlen("sessionId=");
        auto end = cookieHeader.find(';', pos);
        std::string id = cookieHeader.substr(pos, end - pos);
        if (sessions.count(id)) return id;
    }
    // else create
    std::string newId = generateSessionId();
    sessions[newId] = {};
    isNew = true;
    return newId;
}

void SessionManager::addUploadedFile(const std::string &sid, const std::string &fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    sessions[sid].push_back(fn);
}

bool SessionManager::ownsFile(const std::string &sid, const std::string &fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions.find(sid);
    if (it == sessions.end()) return false;
    auto &v = it->second;
    return std::find(v.begin(), v.end(), fn) != v.end();
}

bool SessionManager::removeFile(const std::string &sessionId, const std::string &filename) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions.find(sessionId);
    if (it == sessions.end()) return false;
    auto &v = it->second;
    auto fileIt = std::find(v.begin(), v.end(), filename);
    if (fileIt != v.end()) {
        v.erase(fileIt);
        return true;
    }
    return false;
}

void SessionManager::serialize(const std::string &filename) {
    std::lock_guard<std::mutex> lk(mutex_);
    int fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    pollfd pfd = {fd, POLLOUT, 0};
    if (poll(&pfd, 1, 1000) <= 0) {
        close(fd);
        return;
    }

    auto safeWrite = [fd](const void *data, size_t size) -> bool {
        size_t written = 0;
        while (written < size) {
            ssize_t result = write(fd, static_cast<const char *>(data) + written, size - written);
            if (result <= 0)
                return false;
            written += result;
        }
        return true;
    };

    size_t sessionCount = sessions.size();
    if (!safeWrite(&sessionCount, sizeof(sessionCount))) {
        close(fd);
        return;
    }

    for (const auto &pair: sessions) {
        size_t sidLen = pair.first.size();
        if (!safeWrite(&sidLen, sizeof(sidLen)) ||
            !safeWrite(pair.first.data(), sidLen)) {
            close(fd);
            return;
        }

        size_t fileCount = pair.second.size();
        if (!safeWrite(&fileCount, sizeof(fileCount))) {
            close(fd);
            return;
        }

        for (const std::string &fn: pair.second) {
            size_t fnLen = fn.size();
            if (!safeWrite(&fnLen, sizeof(fnLen)) ||
                !safeWrite(fn.data(), fnLen)) {
                close(fd);
                return;
            }
        }
    }

    close(fd);
}

void SessionManager::deserialize(const std::string &filename) {
    std::lock_guard<std::mutex> lk(mutex_);
    int fd = open(filename.c_str(), O_RDONLY);
    if (fd < 0) return;
    struct pollfd pfd = {fd, POLLIN, 0};
    if (poll(&pfd, 1, 1000) <= 0) {
        close(fd);
        return;
    }
    sessions.clear();
    size_t sessionCount = 0;
    ssize_t r = read(fd, &sessionCount, sizeof(sessionCount));
    if (r != (ssize_t) sizeof(sessionCount)) {
        close(fd);
        return;
    }
    for (size_t i = 0; i < sessionCount; ++i) {
        size_t sidLen = 0;
        if (read(fd, &sidLen, sizeof(sidLen)) != (ssize_t) sizeof(sidLen)) {
            close(fd);
            sessions.clear();
            return;
        }
        if (sidLen > 4096) {
            close(fd);
            sessions.clear();
            return;
        } // sanity check
        std::string sid(sidLen, '\0');
        if (read(fd, &sid[0], sidLen) != (ssize_t) sidLen) {
            close(fd);
            sessions.clear();
            return;
        }
        size_t fileCount = 0;
        if (read(fd, &fileCount, sizeof(fileCount)) != (ssize_t) sizeof(fileCount)) {
            close(fd);
            sessions.clear();
            return;
        }
        if (fileCount > 10000) {
            close(fd);
            sessions.clear();
            return;
        } // sanity check
        std::vector<std::string> files;
        for (size_t j = 0; j < fileCount; ++j) {
            size_t fnLen = 0;
            if (read(fd, &fnLen, sizeof(fnLen)) != (ssize_t) sizeof(fnLen)) {
                close(fd);
                sessions.clear();
                return;
            }
            if (fnLen > 4096) {
                close(fd);
                sessions.clear();
                return;
            } // sanity check
            std::string fn(fnLen, '\0');
            if (read(fd, &fn[0], fnLen) != (ssize_t) fnLen) {
                close(fd);
                sessions.clear();
                return;
            }
            files.push_back(fn);
        }
        sessions[sid] = files;
    }

    close(fd);
}
