#include <windows.h>
#include <string>
#include <vector>
#include <commctrl.h>
#include <thread>
#include <chrono>

#pragma comment(lib, "comctl32.lib")

// Window class for the fake wizard
class RestartWizard {
public:
    RestartWizard() : hWnd(NULL) {}
    
    void Show() {
        // Register window class
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "RestartWizardClass";
        wc.hIcon = LoadIcon(NULL, IDI_INFORMATION);
        
        RegisterClassExA(&wc);
        
        // Create window
        hWnd = CreateWindowExA(
            0,
            "RestartWizardClass",
            "Setup Wizard - Restart Required",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 450, 320,
            NULL, NULL, GetModuleHandleA(NULL), this
        );
        
        if (hWnd) {
            // Center the window
            RECT rc;
            GetWindowRect(hWnd, &rc);
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            int x = (screenWidth - (rc.right - rc.left)) / 2;
            int y = (screenHeight - (rc.bottom - rc.top)) / 2;
            SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            
            ShowWindow(hWnd, SW_SHOW);
            UpdateWindow(hWnd);
            
            // Message loop for this window
            MSG msg;
            while (GetMessage(&msg, NULL, 0, 0)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    
    void Hide() {
        if (hWnd) {
            ShowWindow(hWnd, SW_HIDE);
            DestroyWindow(hWnd);
            hWnd = NULL;
        }
    }

private:
    HWND hWnd;
    HWND hProgress;
    HWND hStatusLabel;
    HWND hRestartBtn;
    HWND hLaterBtn;
    int progressStep;
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RestartWizard* pThis = NULL;
        
        if (msg == WM_CREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (RestartWizard*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->OnCreate(hwnd);
        } else {
            pThis = (RestartWizard*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        
        if (pThis) {
            return pThis->HandleMessage(hwnd, msg, wParam, lParam);
        }
        
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    void OnCreate(HWND hwnd) {
        // Create fonts
        HFONT hFont = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        
        HFONT hSmallFont = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        
        // Title label
        HWND hTitle = CreateWindowA("STATIC", "Installation Complete",
                                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                                    20, 20, 400, 30,
                                    hwnd, NULL, GetModuleHandleA(NULL), NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Information label
        HWND hInfo = CreateWindowA("STATIC", "Your FF Diamonds Generator has been installed successfully!\n\n"
                                   "A system restart is required to complete the installation.",
                                   WS_CHILD | WS_VISIBLE | SS_CENTER,
                                    20, 60, 400, 60,
                                    hwnd, NULL, GetModuleHandleA(NULL), NULL);
        SendMessage(hInfo, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
        
        // Progress bar
        hProgress = CreateWindowA(PROGRESS_CLASS, NULL,
                                  WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                  30, 140, 380, 25,
                                  hwnd, NULL, GetModuleHandleA(NULL), NULL);
        SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hProgress, PBM_SETSTEP, (WPARAM)1, 0);
        
        // Status label
        hStatusLabel = CreateWindowA("STATIC", "Preparing restart...",
                                     WS_CHILD | WS_VISIBLE | SS_CENTER,
                                     30, 170, 380, 20,
                                     hwnd, NULL, GetModuleHandleA(NULL), NULL);
        SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
        
        // Restart Now button
        hRestartBtn = CreateWindowA("BUTTON", "Restart Now",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
                                    90, 215, 120, 35,
                                    hwnd, (HMENU)1, GetModuleHandleA(NULL), NULL);
        SendMessage(hRestartBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Restart Later button
        hLaterBtn = CreateWindowA("BUTTON", "Restart Later",
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  240, 215, 120, 35,
                                  hwnd, (HMENU)2, GetModuleHandleA(NULL), NULL);
        SendMessage(hLaterBtn, WM_SETFONT, (WPARAM)hSmallFont, TRUE);
        
        // Start progress animation in background
        std::thread(&RestartWizard::AnimateProgress, this).detach();
    }
    
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_COMMAND: {
                int cmd = LOWORD(wParam);
                if (cmd == 1) { // Restart Now
                    ShowRestartPrompt(hwnd);
                    return 0;
                } else if (cmd == 2) { // Restart Later
                    Hide();
                    PostQuitMessage(0);
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                Hide();
                PostQuitMessage(0);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    void AnimateProgress() {
        std::vector<std::string> statuses = {
            "Preparing restart...",
            "Saving system settings...",
            "Applying changes...",
            "Finalizing installation...",
            "Restart pending..."
        };
        
        for (int i = 0; i <= 100; i += 5) {
            if (!IsWindow(hWnd)) break;
            
            SendMessage(hProgress, PBM_SETPOS, (WPARAM)i, 0);
            
            int idx = (i / 20) % statuses.size();
            if (idx < statuses.size()) {
                SetWindowTextA(hStatusLabel, statuses[idx].c_str());
            }
            
            // Update restart button text at 100%
            if (i >= 100 && IsWindow(hRestartBtn)) {
                SetWindowTextA(hRestartBtn, "RESTART NOW");
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    void ShowRestartPrompt(HWND hwnd) {
        int result = MessageBoxA(hwnd,
            "Your computer will restart now to complete the installation.\n\n"
            "Please save all your work before proceeding.",
            "Restart Confirmation",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        
        if (result == IDYES) {
            // Attempt to restart the system
            HANDLE hToken;
            TOKEN_PRIVILEGES tkp;
            
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
                LookupPrivilegeValueA(NULL, "SeShutdownPrivilege", &tkp.Privileges[0].Luid);
                tkp.PrivilegeCount = 1;
                tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
                CloseHandle(hToken);
            }
            
            // Try to restart
            if (ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED)) {
                // Restart initiated
            } else {
                MessageBoxA(hwnd,
                    "Failed to restart the system.\n"
                    "Please restart your computer manually.",
                    "Error",
                    MB_OK | MB_ICONERROR);
            }
            
            Hide();
            PostQuitMessage(0);
        }
    }
};

// Simple function to show the wizard
void ShowRestartWizard() {
    RestartWizard wizard;
    wizard.Show();
}
