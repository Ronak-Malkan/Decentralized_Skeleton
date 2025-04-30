#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <mutex>
#include <string>

// A single mutex to guard all console output
inline std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

// Thread-safe print: acquires the mutex, writes the entire msg, then flushes
inline void log_line(const std::string& msg) {
    std::lock_guard<std::mutex> lk(log_mutex());
    std::cout << msg << std::endl;
}

#endif // LOGGER_H
