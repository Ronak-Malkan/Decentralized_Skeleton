#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <mutex>
#include <string>

// Single mutex for all console output
inline std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

// Thread-safe print: locks, prints one line, then unlocks
inline void log_line(const std::string& msg) {
    std::lock_guard<std::mutex> lk(log_mutex());
    std::cout << msg << std::endl;
}

#endif // LOGGER_H
