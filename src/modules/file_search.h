#ifndef FILE_SEARCH_H
#define FILE_SEARCH_H

#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;

struct FileSearchResult {
    std::string path;
    std::string filename;
    uintmax_t size;
    std::string extension;
    fs::file_time_type modified_time;
};

class FileSearch {
public:
    FileSearch();
    ~FileSearch();

    std::vector<FileSearchResult> SearchFiles(
        const std::string& root_path,
        const std::string& pattern,
        bool recursive = true,
        bool case_sensitive = false
    );

    std::vector<FileSearchResult> SearchByExtension(
        const std::string& root_path,
        const std::vector<std::string>& extensions,
        bool recursive = true
    );

    std::vector<FileSearchResult> SearchBySize(
        const std::string& root_path,
        uintmax_t min_size,
        uintmax_t max_size = 0,
        bool recursive = true
    );

    std::vector<FileSearchResult> SearchByModifiedDate(
        const std::string& root_path,
        const std::string& start_date,
        const std::string& end_date = "",
        bool recursive = true
    );

    bool SearchAsync(const std::string& root_path, const std::string& pattern);
    bool IsSearching() const;
    void CancelSearch();
    std::vector<FileSearchResult> GetResults();

private:
    std::thread search_thread;
    std::atomic<bool> is_searching;
    std::atomic<bool> cancel_search;
    std::vector<FileSearchResult> results;
    mutable CRITICAL_SECTION cs;

    bool MatchPattern(const std::string& filename, const std::string& pattern, bool case_sensitive);
    std::string GetFileExtension(const std::string& path);
    void SearchDirectory(const fs::path& path, const std::string& pattern, bool recursive, bool case_sensitive);
    fs::file_time_type ParseDate(const std::string& date_str);
};

#endif // FILE_SEARCH_H
