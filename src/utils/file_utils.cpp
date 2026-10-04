#include "file_utils.h"
#include "logger.h"
#include <fstream>
#include <filesystem>
#include <sstream>
namespace fs = std::filesystem;

bool FileUtils::read_file(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    data.resize(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();
    return true;
}

bool FileUtils::write_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    return true;
}

bool FileUtils::write_file(const std::string& path, const std::string& data) {
    std::vector<uint8_t> vec(data.begin(), data.end());
    return write_file(path, vec);
}

bool FileUtils::file_exists(const std::string& path) {
    return fs::exists(path);
}

bool FileUtils::delete_file(const std::string& path) {
    try {
        return fs::remove(path);
    } catch (...) {
        return false;
    }
}

std::string FileUtils::get_file_name(const std::string& path) {
    return fs::path(path).filename().string();
}

std::string FileUtils::get_file_extension(const std::string& path) {
    return fs::path(path).extension().string();
}

uint64_t FileUtils::get_file_size(const std::string& path) {
    try {
        return fs::file_size(path);
    } catch (...) {
        return 0;
    }
}

bool FileUtils::create_directory(const std::string& path) {
    try {
        return fs::create_directories(path);
    } catch (...) {
        return false;
    }
}
