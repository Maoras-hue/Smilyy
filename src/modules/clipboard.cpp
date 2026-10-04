#include "clipboard.h"
#include <iostream>
#include <algorithm>

ClipboardMonitor::ClipboardMonitor() : is_running(false) {
    InitializeCriticalSection(&cs);
}

ClipboardMonitor::~ClipboardMonitor() {
    StopMonitoring();
    DeleteCriticalSection(&cs);
}

bool ClipboardMonitor::StartMonitoring(int interval_ms) {
    if (is_running) return false;
    
    is_running = true;
    monitor_thread = std::thread(&ClipboardMonitor::MonitorLoop, this, interval_ms);
    return true;
}

void ClipboardMonitor::StopMonitoring() {
    is_running = false;
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
}

void ClipboardMonitor::MonitorLoop(int interval_ms) {
    while (is_running) {
        std::string current = ReadClipboard();
        if (!current.empty() && current != last_content) {
            EnterCriticalSection(&cs);
            last_content = current;
            AddToHistory(current);
            LeaveCriticalSection(&cs);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

std::string ClipboardMonitor::ReadClipboard() {
    std::string result;
    
    if (!OpenClipboard(nullptr)) return result;
    
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData) {
        char* pszText = static_cast<char*>(GlobalLock(hData));
        if (pszText) {
            result = pszText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

bool ClipboardMonitor::WriteClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return false;
    
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, text.length() + 1);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    
    memcpy(GlobalLock(hMem), text.c_str(), text.length() + 1);
    GlobalUnlock(hMem);
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return true;
}

void ClipboardMonitor::AddToHistory(const std::string& content) {
    clipboard_history.push_back(content);
    if (clipboard_history.size() > 100) {
        clipboard_history.erase(clipboard_history.begin());
    }
}

std::string ClipboardMonitor::GetLastClipboardContent() const {
    EnterCriticalSection(&cs);
    std::string result = last_content;
    LeaveCriticalSection(&cs);
    return result;
}

std::vector<std::string> ClipboardMonitor::GetClipboardHistory(int count) const {
    EnterCriticalSection(&cs);
    std::vector<std::string> result;
    int start = std::max(0, (int)clipboard_history.size() - count);
    for (int i = start; i < (int)clipboard_history.size(); i++) {
        result.push_back(clipboard_history[i]);
    }
    LeaveCriticalSection(&cs);
    return result;
}

bool ClipboardMonitor::ClearClipboard() {
    EnterCriticalSection(&cs);
    clipboard_history.clear();
    last_content.clear();
    LeaveCriticalSection(&cs);
    return OpenClipboard(nullptr) ? (EmptyClipboard(), CloseClipboard(), true) : false;
}
