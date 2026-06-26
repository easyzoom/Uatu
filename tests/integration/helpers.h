#pragma once
#include <string>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

struct FixtureProcess {
    pid_t pid{-1};

    explicit FixtureProcess(const std::string& exe_path) {
        pid = fork();
        if (pid == 0) {
            execl(exe_path.c_str(), exe_path.c_str(), nullptr);
            _exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }

    ~FixtureProcess() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            waitpid(pid, nullptr, 0);
        }
    }

    FixtureProcess(const FixtureProcess&) = delete;
    FixtureProcess& operator=(const FixtureProcess&) = delete;
};
