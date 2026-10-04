#ifndef FILE_COMMANDS_H
#define FILE_COMMANDS_H

#include <string>
#include <vector>
#include <cstdint>  // Added for uint8_t

class FileCommands {
public:
    static bool list_directory(const std::string& path, std::vector<std::string>& files);
    static bool delete_file(const std::string& path);
    static bool copy_file(const std::string& src, const std::string& dst);
    static bool move_file(const std::string& src, const std::string& dst);
    static bool upload_file(const std::string& path, const std::vector<uint8_t>& data);
    static bool download_file(const std::string& path, std::vector<uint8_t>& data);
    static bool create_directory(const std::string& path);
    static bool delete_directory(const std::string& path);
};

#endif
