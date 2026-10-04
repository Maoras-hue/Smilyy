#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

class ClipboardMonitor {
public:
    ClipboardMonitor();
    ~ClipboardMonitor();

    bool StartMonitoring(int interval_ms = 500);
    void StopMonitoring();
    std::string GetLastClipboardContent() const;
    std::vector<std::string> GetClipboardHistory(int count = 10) const;
    bool ClearClipboard();

private:
    std::thread monitor_thread;
    std::atomic<bool> is_running;
    std::vector<std::string> clipboard_history;
    std::string last_content;
    mutable CRITICAL_SECTION cs;

    void MonitorLoop(int interval_ms);
    std::string ReadClipboard();
    bool WriteClipboard(const std::string& text);
    void AddToHistory(const std::string& content);
};

#endif // CLIPBOARD_H
