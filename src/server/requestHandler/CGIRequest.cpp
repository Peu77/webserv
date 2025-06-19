#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <csignal>
#include <filesystem>
#include <server/FdHandler.h>

#include "common/Logger.h"
#include "RequestHandler.h"
#include "server/ClientConnection.h"

bool RequestHandler::validateCgiEnvironment() const {
    const std::string filePath = getFilePath();
    if (!std::filesystem::exists(cgiPath) || !std::filesystem::is_regular_file(cgiPath) ||
        access(cgiPath.c_str(), X_OK) != 0) {
        Logger::log(LogLevel::ERROR, "CGI interpreter not found or not executable: " + cgiPath);
        return false;
    }


    return true;
}

static bool setupPipes(int input_pipe[2], int output_pipe[2]) {
    if (pipe(input_pipe) < 0 || pipe(output_pipe) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create pipes for CGI");
        return false;
    }

    return true;
}

void RequestHandler::configureCgiChildProcess(int input_pipe[2], int output_pipe[2]) const {
    const std::string filePath = getFilePath();
    dup2(input_pipe[0], STDIN_FILENO);
    dup2(output_pipe[1], STDOUT_FILENO);
    //int devNull = open("/dev/null", O_WRONLY);
    //dup2(devNull, STDERR_FILENO);
    //close(devNull);

    close(input_pipe[0]);
    close(input_pipe[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);

    std::unordered_map<std::string, std::string> env;
    std::string scriptFileName = std::filesystem::path(filePath).filename().string();

    for (const auto &header: request->headers) {
        std::string name = header.first;
        std::string value = header.second;

        std::string envName = "HTTP_";
        for (char c: name) {
            if (c == '-') {
                envName += '_';
            } else {
                envName += std::toupper(c);
            }
        }

        env[envName] = value;
    }

    env["QUERY_STRING"] = request->getQueryString();
    env["REQUEST_METHOD"] = request->getMethodString();
    env["CONTENT_TYPE"] = request->getHeader("Content-Type");
    env["CONTENT_LENGTH"] = std::to_string(request->totalBodySize);
    env["SERVER_PROTOCOL"] = "HTTP/1.1";
    env["SERVER_SOFTWARE"] = "Webserv/1.0";
    env["GATEWAY_INTERFACE"] = "CGI/1.1";
    env["SERVER_NAME"] = serverConfig.host;
    env["SERVER_PORT"] = std::to_string(serverConfig.port);
    env["PATH_INFO"] = request->getPath();
    env["SCRIPT_NAME"] = request->getPath();
    env["REQUEST_URI"] = request->getUri();
    env["SCRIPT_FILENAME"] = scriptFileName;
    env["REDIRECT_STATUS"] = "200";


    std::vector<char *> envp;
    for (const auto &[fst, snd]: env) {
        std::string envVar = fst + "=" + snd;
        envp.push_back(strdup(envVar.data()));
    }
    envp.push_back(nullptr);

    std::filesystem::path path = filePath;
    std::string parentPath = path.parent_path().string();

    chdir(parentPath.c_str());


    char *const argv[] = {const_cast<char *>(cgiPath.c_str()), const_cast<char *>(scriptFileName.c_str()), nullptr};
    execve(cgiPath.c_str(), argv, envp.data());

    Logger::log(LogLevel::ERROR, "Failed to execute CGI script: " + scriptFileName + " with interpreter: " + cgiPath);
    _exit(EXIT_FAILURE);
}

void RequestHandler::cleanupCgiProcess(const pid_t pid) const {
    int status = 0;
    const pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == 0) {
        kill(pid, SIGTERM);
        return;
    }

    if (WIFEXITED(status)) {
        if (const int exitCode = WEXITSTATUS(status); exitCode == 1) {
            Logger::log(LogLevel::ERROR, "CGI process exited with code: " + std::to_string(exitCode));
            const HttpResponse response = HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR,
                                                             "CGI Error: Process exited with code " + std::to_string(
                                                                 exitCode));
            setResponse(response);
            return;
        }

        Logger::log(LogLevel::DEBUG, "CGI process exited successfully");
    }
}

std::optional<HttpResponse> RequestHandler::handleCgi() {
    int input_pipe[2]; // Parent -> Child
    int output_pipe[2]; // Child -> Parent

    if (!setupPipes(input_pipe, output_pipe)) {
        return HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR,
                                  "CGI Error: Could not create pipes");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        Logger::log(LogLevel::ERROR, "Failed to fork process for CGI");
        return HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR,
                                  "CGI Error: Could not fork process");
    }


    if (pid == 0) {
        configureCgiChildProcess(input_pipe, output_pipe);
    }
    Logger::log(LogLevel::DEBUG, "CGI started with PID: " + std::to_string(pid));

    cgiProcessId = pid;

    close(input_pipe[0]);
    close(output_pipe[1]);

    cgiInputFd = input_pipe[1];

    Logger::log(LogLevel::DEBUG, "request->totalBodySize: " + std::to_string(request->totalBodySize) +
                                 " request->body->getSize(): " + std::to_string(request->body->getSize()));

    if (fcntl(cgiInputFd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return 1;
    }

    client->cgiProcessStart = std::time(nullptr);

    FdHandler::addFd(cgiInputFd, POLLOUT | POLLHUP, [this](const int fd, const short events) {
        (void) fd;
        (void) events;

        if (events & POLLHUP) {
            Logger::log(LogLevel::DEBUG, "CGI process closed writing");
            return true;
        }

        if (request->body->isStillWriting())
            return false;


        // TODO: magic number, look at max bytes for pipes to write
        request->body->read(30000);

        if (static_cast<size_t>(bytesWrittenToCgi) >= request->totalBodySize) {
            Logger::log(LogLevel::DEBUG, "Finished writing to CGI process");
            close(fd);
            return true;
        }

        const std::string readBuffer = request->body->getReadBuffer();
        if (!readBuffer.empty()) {
            const ssize_t written = write(fd, readBuffer.data(),
                                          std::min(readBuffer.length(), static_cast<size_t>(60000)));
            if (written <= 0) {
                Logger::log(LogLevel::ERROR, "Failed to write to CGI process");
                close(fd);
                return true;
            }
            bytesWrittenToCgi += written;
            request->body->cleanReadBuffer(written);
        }


        return false;
    });
    cgiOutputFd = output_pipe[0];

    if (fcntl(cgiOutputFd, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return 1;
    }

    FdHandler::addFd(output_pipe[0], POLLIN | POLLHUP, [pid, this](int fd, short events) {
        (void) events;

        ssize_t bytesRead = 0;
        char buffer[60000];
        bytesRead = read(fd, buffer, sizeof(buffer) - 1);
        if (bytesRead == -1)
            return false;
        if (bytesRead >= 0) {
            buffer[bytesRead] = '\0';
        }
        if ((cgiParser.parse(buffer, bytesRead)) || bytesRead == 0) {
            close(fd);
            const auto result = cgiParser.getResult();
            HttpResponse response(HttpResponse::StatusCode::OK);
            for (const auto &header: result.headers) {
                if (header.first == "Status") {
                    const int statusCode = std::stoi(header.second.substr(0, 3));
                    response.setStatus(statusCode);
                } else if (!header.first.empty())
                    response.setHeader(header.first, header.second);
            }
            for (const auto& cookie: result.setCookies)
                response.addSetCookie(cookie);
            response.enableChunkedEncoding(result.body);
            setResponse(response);
            cleanupCgiProcess(pid);
            return true;
        }

        if (cgiParser.hasError()) {
            close(fd);
            cleanupCgiProcess(pid);
            Logger::log(LogLevel::ERROR, "CGI process error parsing error");
            const HttpResponse response = HttpResponse::html(HttpResponse::StatusCode::INTERNAL_SERVER_ERROR,
                                                             "CGI Error: Could not parse output");
            setResponse(response);
            return true;
        }


        if (events & POLLHUP) {
            Logger::log(LogLevel::ERROR, "CGI process error reading");
            const HttpResponse response = HttpResponse::html(HttpResponse::StatusCode::OK,
                                                             "CGI Error: Could not parse output");
            setResponse(response);
            return true;
        }

        return false;
    });
    return std::nullopt;
}
