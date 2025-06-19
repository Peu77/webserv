//
// Created by Emil Ebert on 08.05.25.
//

#include "FdHandler.h"
#include <common/Logger.h>

std::vector<pollfd> FdHandler::pollfds;
std::unordered_map<int, std::function<bool(int, short)> > FdHandler::fdCallbacks;
std::queue<pollfd> FdHandler::fdQueue;

void FdHandler::addFd(const int fd, const short events, const std::function<bool(int, short)> &callback) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    fdQueue.push(pfd);
    fdCallbacks[fd] = callback;
}

std::vector<pollfd>::iterator FdHandler::removeFd(const int fd) {
    const auto it = std::remove_if(pollfds.begin(), pollfds.end(), [fd](const pollfd &pfd) {
        return pfd.fd == fd;
    });

    fdCallbacks.erase(fd);

    if (it != pollfds.end()) {
        return pollfds.erase(it, pollfds.end());
    }
    return pollfds.end();
}


void FdHandler::pollFds() {
    while (!fdQueue.empty() && pollfds.size() < 1024) {
        pollfds.push_back(fdQueue.front());
        fdQueue.pop();
    }

    const int ret = poll(pollfds.data(), pollfds.size(), 100);
    if (ret < 0) {
        Logger::log(LogLevel::ERROR, "Poll error");
        return;
    }

    auto it = pollfds.begin();
    while (it != pollfds.end()) {
        if (it->revents & POLLERR) {
            Logger::log(LogLevel::ERROR, "Poll error on fd: " + std::to_string(it->fd));
            //Logger::log(LogLevel::ERROR, "Error: " + std::string(strerror(errno)));
            it = removeFd(it->fd);
            continue;
        }
        if (it->revents & POLLNVAL) {
            it = removeFd(it->fd);
            continue;
        }
        if (it->revents & POLLIN || it->revents & POLLOUT || it->revents & POLLHUP) {
            if (auto fdCallbacksIt = fdCallbacks.find(it->fd); fdCallbacksIt == fdCallbacks.end()) {
                it = removeFd(it->fd);
                continue;
            }

            try {
                if (fdCallbacks[it->fd](it->fd, it->revents)) {
                    it = removeFd(it->fd);
                    continue;
                }
            }catch (std::exception &e) {
                Logger::log(LogLevel::ERROR, e.what());
            }
        }
        ++it;
    }
}
