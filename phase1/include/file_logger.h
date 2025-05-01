#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <nlohmann/json.hpp>

class FileLogger {
public:
  explicit FileLogger(std::string source)
    : src_(std::move(source)),
      ofs_("logs/" + src_ + ".log", std::ios::app)
  {}

  // log an event with optional data object
  void log(const std::string& event,
           const nlohmann::json::object_t& data = {}) 
  {
    nlohmann::json j;
    j["event"] = event;
    if (!data.empty()) j["data"] = data;
    j["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
              ).count();

    std::lock_guard<std::mutex> lk(mtx_);
    ofs_ << j.dump() << "\n";
  }

private:
  std::string    src_;
  std::ofstream  ofs_;
  std::mutex     mtx_;
};

#endif // FILE_LOGGER_H
