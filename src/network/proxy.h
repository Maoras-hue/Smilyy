#ifndef PROXY_H
#define PROXY_H

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <map>

struct ProxyConfig {
    enum Type { NONE, HTTP, HTTPS, SOCKS4, SOCKS5 };
    
    Type type;
    std::string host;
    int port;
    std::string username;
    std::string password;
    bool use_authentication;
    std::vector<std::string> bypass_list;
    
    ProxyConfig() : type(NONE), port(0), use_authentication(false) {}
};

class Proxy {
public:
    Proxy();
    ~Proxy();

    bool SetProxy(const ProxyConfig& config);
    bool SetProxy(const std::string& proxy_string);
    bool DisableProxy();
    ProxyConfig GetCurrentProxy() const;
    
    bool StartProxyServer(int port);
    bool StopProxyServer();
    bool IsProxyServerRunning() const;
    
    std::vector<ProxyConfig> GetAvailableProxies();
    bool TestProxy(const ProxyConfig& config, int timeout_ms = 5000);
    bool RotateProxy();

    // System-wide proxy settings
    bool SetSystemProxy(const ProxyConfig& config);
    bool ResetSystemProxy();
    bool IsSystemProxyEnabled() const;

private:
    ProxyConfig current_config;
    std::thread proxy_server_thread;
    std::atomic<bool> is_server_running;
    mutable CRITICAL_SECTION cs;
    
    // For system proxy (Windows)
    bool SetWinHTTPProxy(const ProxyConfig& config);
    bool SetIEProxy(const ProxyConfig& config);
    
    // For SOCKS proxy
    bool StartSOCKSServer(int port);
    bool StartHTTPServer(int port);
    
    void ProxyServerLoop(int port);
    bool HandleClient(SOCKET client_socket);
    
    std::map<int, std::string> proxy_list;
    int current_proxy_index;
};

#endif // PROXY_H
