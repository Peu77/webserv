//
// Created by Emil Ebert on 30.04.25.
//

#include <Banner.h>
#include <iostream>
#include "server/ServerPool.h"
#include <csignal>
#include <webserv.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <server/FdHandler.h>
#include <unistd.h>
#include <filesystem>

#include "common/Logger.h"


static void signalHandler(const int signum) {
    ServerPool::stop();
    (void) signum;
}

static void setupSignalHandler() {
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        Logger::log(LogLevel::ERROR, "Failed to set up signal handler");
        exit(1);
    }
}

static void createTempDir() {
    std::filesystem::create_directory(TEMP_DIR_NAME);
}


int main(const int argc, char **argv) {
    if (argc < 2) {
        Logger::log(LogLevel::ERROR, "Not enough arguments");
        std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);


    createTempDir();
    if (!ServerPool::loadConfig(argv[1]))
        return 1;
#ifndef DEBUG_MODE
    Banner::printBannerAnimation();
#endif
    setupSignalHandler();
    ServerPool::start();
    return 0;
}
