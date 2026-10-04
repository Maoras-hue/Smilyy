#include "logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Logger::init(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx);
    log_file.open(filename, std::ios::app);
    initialized = log_file.is_open();
    if (initialized) {
        log("=== LOGGER INITIALIZED ===");
    }
}

void Logger::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!initialized) init();
    if (log_file.is_open()) {
        log_file << "[" << get_timestamp() << "] " << message << std::endl;
        log_file.flush();
    }
}

void Logger::log_error(const std::string& message) {
    log("[ERROR] " + message);
}

void Logger::log_info(const std::string& message) {
    log("[INFO] " + message);
}

void Logger::close() {
    std::lock_guard<std::mutex> lock(mtx);
    if (log_file.is_open()) {
        log("=== LOGGER CLOSED ===");
        log_file.close();
    }
}
