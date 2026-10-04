#ifndef LOGGER_H
#define LOGGER_H
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
class Logger {
private:
    Logger() {}
    ~Logger() {}
    std::ofstream log_file;
    std::mutex mtx;
    bool initialized;
    std::string get_timestamp();
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    void init(const std::string& filename = "rat_log.txt");
    void log(const std::string& message);
    void log_error(const std::string& message);
    void log_info(const std::string& message);
    void close();
};
#endif
