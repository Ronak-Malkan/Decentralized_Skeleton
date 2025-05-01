#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

class FileLogger {
public:
    explicit FileLogger(const std::string& node_id) {
        // Ensure the logs/ directory exists (relative to cwd)
        std::filesystem::create_directories("logs");

        // Open logs/<node_id>.log for append
        std::string path = "logs/" + node_id + ".log";
        file_.open(path, std::ios::out | std::ios::app);
        if (!file_) {
            throw std::runtime_error("Failed to open log file: " + path);
        }
    }

    // Thread-safe JSON-line logging; also echoes to console
    void log(const std::string& event, const nlohmann::json& details = {}) {
        // Timestamp in ms since epoch
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()).count();

        nlohmann::json obj;
        obj["ts"]    = ms;
        obj["event"] = event;
        if (!details.empty()) obj["data"] = details;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            // Write + flush
            file_ << obj.dump() << std::endl;
        }

        // Console echo (unaffected)
        std::cout << "[" << event << "] " << obj.dump() << std::endl;
    }

private:
    std::ofstream file_;
    std::mutex    mtx_;
};

#endif // FILE_LOGGER_H
