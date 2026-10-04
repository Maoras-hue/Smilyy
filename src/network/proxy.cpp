#include "proxy.h"
#include <sstream>
#include <regex>
#include <cstdlib>
#include <shlwapi.h>
#include <winreg.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")

Proxy::Proxy() : is_server_running(false), current_proxy_index(0) {
    InitializeCriticalSection(&cs);
    
    // Initialize proxy list (just examples)
    proxy_list = {
        {1, "http://proxy1.example.com:8080"},
        {2, "http://proxy2.example.com:8080"},
        {3, "socks5://proxy3.example.com:1080"}
    };
}

Proxy::~Proxy() {
    StopProxyServer();
    DeleteCriticalSection(&cs);
}

bool Proxy::SetProxy(const ProxyConfig& config) {
    EnterCriticalSection(&cs);
    current_config = config;
    LeaveCriticalSection(&cs);
    
    // Apply to system if requested
    return SetSystemProxy(config);
}

bool Proxy::SetProxy(const std::string& proxy_string) {
    ProxyConfig config;
    config.type = ProxyConfig::HTTP;
    config.use_authentication = false;
    
    // Parse proxy string (e.g., "http://user:pass@host:port")
    std::regex re(R"((\w+)://(?:(.+?):(.+?)@)?([^:]+):(\d+))");
    std::smatch match;
    
    if (std::regex_match(proxy_string, match, re)) {
        std::string protocol = match[1];
        if (protocol == "http") config.type = ProxyConfig::HTTP;
        else if (protocol == "https") config.type = ProxyConfig::HTTPS;
        else if (protocol == "socks4") config.type = ProxyConfig::SOCKS4;
        else if (protocol == "socks5") config.type = ProxyConfig::SOCKS5;
        
        if (match[2].matched) {
            config.username = match[2];
            config.password = match[3];
            config.use_authentication = true;
        }
        
        config.host = match[4];
        config.port = std::stoi(match[5]);
        
        return SetProxy(config);
    }
    
    return false;
}

bool Proxy::SetSystemProxy(const ProxyConfig& config) {
    // Set IE/Windows proxy
    return SetIEProxy(config);
}

bool Proxy::SetIEProxy(const ProxyConfig& config) {
    HKEY hKey;
    LPCSTR subkey = "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    // Enable/disable proxy
    DWORD enable = (config.type != ProxyConfig::NONE) ? 1 : 0;
    RegSetValueExA(hKey, "ProxyEnable", 0, REG_DWORD, (BYTE*)&enable, sizeof(enable));
    
    // Set proxy server
    if (config.type != ProxyConfig::NONE && !config.host.empty()) {
        std::string server = config.host + ":" + std::to_string(config.port);
        RegSetValueExA(hKey, "ProxyServer", 0, REG_SZ, (BYTE*)server.c_str(), server.length() + 1);
        
        // Set authentication if needed
        if (config.use_authentication) {
            RegSetValueExA(hKey, "ProxyUser", 0, REG_SZ, (BYTE*)config.username.c_str(), config.username.length() + 1);
            RegSetValueExA(hKey, "ProxyPass", 0, REG_SZ, (BYTE*)config.password.c_str(), config.password.length() + 1);
        }
    } else {
        // Clear proxy settings
        RegDeleteValueA(hKey, "ProxyServer");
        RegDeleteValueA(hKey, "ProxyUser");
        RegDeleteValueA(hKey, "ProxyPass");
    }
    
    RegCloseKey(hKey);
    
    // Broadcast settings change to system
    DWORD_PTR result;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Internet Settings",
                        SMTO_NORMAL, 1000, &result);
    
    return true;
}

bool Proxy::DisableProxy() {
    ProxyConfig config;
    config.type = ProxyConfig::NONE;
    return SetProxy(config);
}

ProxyConfig Proxy::GetCurrentProxy() const {
    EnterCriticalSection(&cs);
    ProxyConfig config = current_config;
    LeaveCriticalSection(&cs);
    return config;
}

bool Proxy::StartProxyServer(int port) {
    if (is_server_running) return false;
    
    is_server_running = true;
    proxy_server_thread = std::thread(&Proxy::ProxyServerLoop, this, port);
    
    return true;
}

bool Proxy::StopProxyServer() {
    is_server_running = false;
    if (proxy_server_thread.joinable()) {
        proxy_server_thread.join();
    }
    return true;
}

void Proxy::ProxyServerLoop(int port) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        is_server_running = false;
        return;
    }
    
    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == INVALID_SOCKET) {
        WSACleanup();
        is_server_running = false;
        return;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    // Bind to port
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(listen_socket);
        WSACleanup();
        is_server_running = false;
        return;
    }
    
    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_socket);
        WSACleanup();
        is_server_running = false;
        return;
    }
    
    while (is_server_running) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_socket != INVALID_SOCKET) {
            // For now, just close the connection
            // In a full implementation, you'd handle the proxy connection here
            closesocket(client_socket);
        }
    }
    
    closesocket(listen_socket);
    WSACleanup();
}

bool Proxy::TestProxy(const ProxyConfig& config, int timeout_ms) {
    // Test proxy by making a connection
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    
    // Set timeout
    int timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    // Resolve host
    struct addrinfo hints, *result = NULL;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    char port_str[16];
    sprintf(port_str, "%d", config.port);
    
    int ret = getaddrinfo(config.host.c_str(), port_str, &hints, &result);
    if (ret != 0) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    // Try to connect
    bool connected = false;
    for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
        if (connect(sock, ptr->ai_addr, ptr->ai_addrlen) == 0) {
            connected = true;
            break;
        }
    }
    
    freeaddrinfo(result);
    closesocket(sock);
    WSACleanup();
    
    return connected;
}

bool Proxy::RotateProxy() {
    if (proxy_list.empty()) return false;
    
    current_proxy_index = (current_proxy_index + 1) % proxy_list.size();
    auto it = proxy_list.find(current_proxy_index);
    if (it != proxy_list.end()) {
        return SetProxy(it->second);
    }
    return false;
}

std::vector<ProxyConfig> Proxy::GetAvailableProxies() {
    std::vector<ProxyConfig> proxies;
    // Return configured proxies
    ProxyConfig config;
    config.type = ProxyConfig::HTTP;
    config.host = "localhost";
    config.port = 8080;
    proxies.push_back(config);
    return proxies;
}

bool Proxy::IsProxyServerRunning() const {
    return is_server_running;
}

bool Proxy::ResetSystemProxy() {
    ProxyConfig config;
    config.type = ProxyConfig::NONE;
    return SetIEProxy(config);
}

bool Proxy::IsSystemProxyEnabled() const {
    HKEY hKey;
    DWORD enabled = 0;
    DWORD size = sizeof(DWORD);
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, 
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProxyEnable", NULL, NULL, (BYTE*)&enabled, &size);
        RegCloseKey(hKey);
    }
    
    return enabled == 1;
}
