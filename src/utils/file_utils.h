#ifndef FILE_UTILS_H
#define FILE_UTILS_H
#include <string>
#include <vector>
#include <cstdint>
class FileUtils {
public:
    static bool read_file(const std::string& path, std::vector<uint8_t>& data);
    static bool write_file(const std::string& path, const std::vector<uint8_t>& data);
    static bool write_file(const std::string& path, const std::string& data);
    static bool file_exists(const std::string& path);
    static bool delete_file(const std::string& path);
    static std::string get_file_name(const std::string& path);
    static std::string get_file_extension(const std::string& path);
    static uint64_t get_file_size(const std::string& path);
    static bool create_directory(const std::string& path);
};
#endif
