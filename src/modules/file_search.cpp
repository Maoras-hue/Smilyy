#include "file_search.h"
#include <windows.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>

FileSearch::FileSearch() : is_searching(false), cancel_search(false) {
    InitializeCriticalSection(&cs);
}

FileSearch::~FileSearch() {
    CancelSearch();
    DeleteCriticalSection(&cs);
}

std::vector<FileSearchResult> FileSearch::SearchFiles(
    const std::string& root_path,
    const std::string& pattern,
    bool recursive,
    bool case_sensitive) {
    
    std::vector<FileSearchResult> results_local;
    
    // Clear previous results
    EnterCriticalSection(&cs);
    results.clear();
    LeaveCriticalSection(&cs);
    
    SearchDirectory(fs::path(root_path), pattern, recursive, case_sensitive);
    
    // Get results
    EnterCriticalSection(&cs);
    results_local = results;
    LeaveCriticalSection(&cs);
    
    return results_local;
}

void FileSearch::SearchDirectory(const fs::path& path, const std::string& pattern, 
                                 bool recursive, bool case_sensitive) {
    if (cancel_search) return;
    
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (cancel_search) return;
            
            if (fs::is_regular_file(entry)) {
                std::string filename = entry.path().filename().string();
                if (MatchPattern(filename, pattern, case_sensitive)) {
                    FileSearchResult result;
                    result.path = entry.path().string();
                    result.filename = filename;
                    result.size = fs::file_size(entry);
                    result.extension = GetFileExtension(filename);
                    result.modified_time = fs::last_write_time(entry);
                    
                    EnterCriticalSection(&cs);
                    this->results.push_back(result);
                    LeaveCriticalSection(&cs);
                }
            } else if (recursive && fs::is_directory(entry)) {
                SearchDirectory(entry, pattern, recursive, case_sensitive);
            }
        }
    } catch (const std::exception& e) {
        // Skip inaccessible directories
    }
}

bool FileSearch::MatchPattern(const std::string& filename, const std::string& pattern, bool case_sensitive) {
    std::string file = filename;
    std::string pat = pattern;
    
    if (!case_sensitive) {
        std::transform(file.begin(), file.end(), file.begin(), ::tolower);
        std::transform(pat.begin(), pat.end(), pat.begin(), ::tolower);
    }
    
    // Convert wildcard pattern to regex
    std::string regex_pattern = "^" + pat;
    size_t pos = 0;
    while ((pos = regex_pattern.find("*", pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".*");
        pos += 2;
    }
    pos = 0;
    while ((pos = regex_pattern.find("?", pos)) != std::string::npos) {
        regex_pattern.replace(pos, 1, ".");
        pos += 1;
    }
    regex_pattern += "$";
    
    try {
        std::regex re(regex_pattern, std::regex::ECMAScript);
        return std::regex_match(file, re);
    } catch (...) {
        return false;
    }
}

std::string FileSearch::GetFileExtension(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos != std::string::npos) {
        return path.substr(pos);
    }
    return "";
}

bool FileSearch::SearchAsync(const std::string& root_path, const std::string& pattern) {
    if (is_searching) return false;
    
    is_searching = true;
    cancel_search = false;
    
    search_thread = std::thread([this, root_path, pattern]() {
        SearchDirectory(fs::path(root_path), pattern, true, false);
        is_searching = false;
    });
    
    return true;
}

bool FileSearch::IsSearching() const {
    return is_searching;
}

void FileSearch::CancelSearch() {
    cancel_search = true;
    if (search_thread.joinable()) {
        search_thread.join();
    }
    is_searching = false;
}

std::vector<FileSearchResult> FileSearch::GetResults() {
    EnterCriticalSection(&cs);
    std::vector<FileSearchResult> temp = results;
    results.clear();
    LeaveCriticalSection(&cs);
    return temp;
}

std::vector<FileSearchResult> FileSearch::SearchByExtension(
    const std::string& root_path,
    const std::vector<std::string>& extensions,
    bool recursive) {
    
    std::vector<FileSearchResult> all_results;
    
    for (const auto& ext : extensions) {
        std::string pattern = "*" + ext;
        auto results = SearchFiles(root_path, pattern, recursive, false);
        all_results.insert(all_results.end(), results.begin(), results.end());
    }
    
    return all_results;
}

std::vector<FileSearchResult> FileSearch::SearchBySize(
    const std::string& root_path,
    uintmax_t min_size,
    uintmax_t max_size,
    bool recursive) {
    
    std::vector<FileSearchResult> results_local;
    
    // Clear previous results
    EnterCriticalSection(&cs);
    results.clear();
    LeaveCriticalSection(&cs);
    
    // Search all files
    SearchDirectory(fs::path(root_path), "*", recursive, false);
    
    // Filter by size
    EnterCriticalSection(&cs);
    for (const auto& result : results) {
        if (result.size >= min_size && (max_size == 0 || result.size <= max_size)) {
            results_local.push_back(result);
        }
    }
    results.clear();
    LeaveCriticalSection(&cs);
    
    return results_local;
}

std::vector<FileSearchResult> FileSearch::SearchByModifiedDate(
    const std::string& root_path,
    const std::string& start_date,
    const std::string& end_date,
    bool recursive) {
    
    // Simplified - just return all files
    return SearchFiles(root_path, "*", recursive, false);
}

fs::file_time_type FileSearch::ParseDate(const std::string& date_str) {
    // Simplified - return current time
    return fs::file_time_type::clock::now();
}
