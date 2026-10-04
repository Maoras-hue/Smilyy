#include "file_commands.h"
#include "../utils/logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>  // Added

namespace fs = std::filesystem;

bool FileCommands::list_directory(const std::string& path, std::vector<std::string>& files) {
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            files.push_back(entry.path().string());
        }
        return true;
    } catch (const std::exception& e) {
        Logger::getInstance().log("List directory error: " + std::string(e.what()));
        return false;
    }
}

bool FileCommands::delete_file(const std::string& path) {
    try {
        return fs::remove(path);
    } catch (const std::exception& e) {
        Logger::getInstance().log("Delete file error: " + std::string(e.what()));
        return false;
    }
}

bool FileCommands::copy_file(const std::string& src, const std::string& dst) {
    try {
        fs::copy(src, dst, fs::copy_options::overwrite_existing);
        return true;
    } catch (const std::exception& e) {
        Logger::getInstance().log("Copy file error: " + std::string(e.what()));
        return false;
    }
}

bool FileCommands::move_file(const std::string& src, const std::string& dst) {
    try {
        fs::rename(src, dst);
        return true;
    } catch (const std::exception& e) {
        Logger::getInstance().log("Move file error: " + std::string(e.what()));
        return false;
    }
}

bool FileCommands::upload_file(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    Logger::getInstance().log("File uploaded: " + path);
    return true;
}

bool FileCommands::download_file(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    data.resize(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();
    Logger::getInstance().log("File downloaded: " + path);
    return true;
}

bool FileCommands::create_directory(const std::string& path) {
    try {
        return fs::create_directories(path);
    } catch (const std::exception& e) {
        Logger::getInstance().log("Create directory error: " + std::string(e.what()));
        return false;
    }
}

bool FileCommands::delete_directory(const std::string& path) {
    try {
        return fs::remove_all(path) > 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log("Delete directory error: " + std::string(e.what()));
        return false;
    }
}
