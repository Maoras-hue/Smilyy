#ifndef ANTI_DEBUG_H
#define ANTI_DEBUG_H
#include <windows.h>
#include <string>
class AntiDebug {
private:
    AntiDebug() {}
    ~AntiDebug() {}
public:
    static AntiDebug& getInstance() {
        static AntiDebug instance;
        return instance;
    }
    bool is_debugger_present();
    bool check_remote_debugger();
    void prevent_debugging();
    void check_for_debugger_and_exit();
};
#endif
