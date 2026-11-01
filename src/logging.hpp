#pragma once
#include "config.hpp"
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
namespace csad {
// Only service-generated metadata is accepted by callers; never audio/user text.
class Logger {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Json> queue;
    std::thread thread;
    bool stopped = false;
    uint64_t dropped = 0;
    std::filesystem::path path;
    size_t maximum, retained;
    void run() {
        try {
            std::ofstream file;
            auto open = [&] { file.open(path, std::ios::app); if (!file) throw std::runtime_error("logging"); };
            open();
            size_t bytes = std::filesystem::file_size(path);
            for (;;) {
                Json event;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return stopped || !queue.empty(); });
                    if (queue.empty()) return;
                    event = std::move(queue.front()); queue.pop_front(); event["dropped_log_count"] = std::to_string(dropped);
                }
                auto line = event.dump() + '\n';
                if (bytes + line.size() > maximum) {
                    file.close();
                    for (size_t n = retained; n > 0; --n) {
                        auto from = n == 1 ? path : std::filesystem::path(path.string() + "." + std::to_string(n - 1));
                        auto to = path.string() + "." + std::to_string(n);
                        if (std::filesystem::exists(from)) { std::filesystem::remove(to); std::filesystem::rename(from, to); }
                    }
                    open(); bytes = 0;
                }
                file << line; file.flush(); if (!file) throw std::runtime_error("logging"); bytes += line.size();
            }
        } catch (...) {
            std::cerr << "warning: debug_logging_disabled\n";
            std::lock_guard lock(mutex); stopped = true; queue.clear();
        }
    }
public:
    explicit Logger(const Config& config) : path(config.value["logging"]["file"].get<std::string>()),
        maximum(config.get("logging", "maximum_file_bytes")), retained(config.get("logging", "retained_files")) {
        if (config.value["logging"]["debug_enabled"].get<bool>()) thread = std::thread([this] { run(); });
    }
    ~Logger() { { std::lock_guard lock(mutex); stopped = true; changed.notify_all(); } if (thread.joinable()) thread.join(); }
    void event(Json value) {
        if (!thread.joinable()) return;
        std::lock_guard lock(mutex);
        if (stopped || queue.size() >= 128) { ++dropped; return; }
        queue.push_back(std::move(value)); changed.notify_one();
    }
};
}
