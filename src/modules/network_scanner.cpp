#include "network_scanner.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <algorithm>
#include <regex>

NetworkScanner::NetworkScanner() : is_scanning(false), stop_scan(false) {
    InitializeCriticalSection(&cs);
    
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
}

NetworkScanner::~NetworkScanner() {
    StopScan();
    DeleteCriticalSection(&cs);
    WSACleanup();
}

std::vector<NetworkDevice> NetworkScanner::ScanNetwork(
    const std::string& subnet,
    int timeout_ms,
    int max_threads) {
    
    std::vector<NetworkDevice> results;
    std::vector<std::string> ips = GetIPsInSubnet(subnet);
    
    std::vector<std::thread> threads;
    std::atomic<int> processed{0};
    
    for (const auto& ip : ips) {
        if (stop_scan) break;
        
        threads.emplace_back([this, ip, timeout_ms, &results, &processed]() {
            NetworkDevice device;
            device.ip_address = ip;
            device.is_active = PingHost(ip, timeout_ms);
            
            if (device.is_active) {
                device.hostname = GetHostname(ip);
                device.mac_address = GetMACAddress(ip);
                device.operating_system = PerformOSFingerprint(ip);
                device.response_time_ms = timeout_ms;
                
                // Scan common ports
                std::vector<int> common_ports = {21, 22, 23, 25, 53, 80, 110, 143, 443, 445, 3306, 3389, 8080};
                for (int port : common_ports) {
                    if (CheckPort(ip, port, 500)) {
                        device.open_ports.push_back(port);
                    }
                }
            }
            
            EnterCriticalSection(&cs);
            results.push_back(device);
            LeaveCriticalSection(&cs);
            processed++;
        });
        
        if (threads.size() >= max_threads) {
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }
            threads.clear();
        }
    }
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    
    EnterCriticalSection(&cs);
    devices = results;
    LeaveCriticalSection(&cs);
    
    return results;
}

std::vector<std::string> NetworkScanner::GetIPsInSubnet(const std::string& subnet) {
    std::vector<std::string> ips;
    
    // Parse subnet (e.g., "192.168.1.0/24")
    size_t slash = subnet.find('/');
    if (slash == std::string::npos) {
        // If no subnet mask, assume /24
        ips.push_back(subnet);
        return ips;
    }
    
    std::string ip_str = subnet.substr(0, slash);
    int prefix = std::stoi(subnet.substr(slash + 1));
    
    uint32_t ip = IPToUInt(ip_str);
    uint32_t mask = (prefix == 0) ? 0 : ~((1 << (32 - prefix)) - 1);
    uint32_t network = ip & mask;
    uint32_t broadcast = network | ~mask;
    
    for (uint32_t i = network + 1; i < broadcast; i++) {
        ips.push_back(UIntToIP(i));
    }
    
    return ips;
}

bool NetworkScanner::PingHost(const std::string& ip, int timeout_ms) {
    // Use system ping command
    #ifdef _WIN32
    std::string cmd = "ping -n 1 -w " + std::to_string(timeout_ms) + " " + ip + " > nul 2>&1";
    #else
    std::string cmd = "ping -c 1 -W " + std::to_string(timeout_ms/1000) + " " + ip + " > /dev/null 2>&1";
    #endif
    
    return system(cmd.c_str()) == 0;
}

std::string NetworkScanner::GetHostname(const std::string& ip) {
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    inet_pton(AF_INET, ip.c_str(), &sa.sin_addr);
    
    char host[NI_MAXHOST];
    int result = getnameinfo((struct sockaddr*)&sa, sizeof(sa), host, NI_MAXHOST, nullptr, 0, 0);
    
    if (result == 0) {
        return std::string(host);
    }
    return "";
}

std::string NetworkScanner::GetMACAddress(const std::string& ip) {
    std::string mac = "";
    
    ULONG outBufLen = sizeof(IP_ADAPTER_INFO);
    IP_ADAPTER_INFO* pAdapterInfo = (IP_ADAPTER_INFO*)malloc(outBufLen);
    
    if (GetAdaptersInfo(pAdapterInfo, &outBufLen) == ERROR_BUFFER_OVERFLOW) {
        free(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO*)malloc(outBufLen);
    }
    
    if (GetAdaptersInfo(pAdapterInfo, &outBufLen) == NO_ERROR) {
        IP_ADAPTER_INFO* pAdapter = pAdapterInfo;
        while (pAdapter) {
            // Compare IPs
            std::string adapter_ip = pAdapter->IpAddressList.IpAddress.String;
            if (ip == adapter_ip) {
                char macStr[18];
                sprintf_s(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                    pAdapter->Address[0], pAdapter->Address[1],
                    pAdapter->Address[2], pAdapter->Address[3],
                    pAdapter->Address[4], pAdapter->Address[5]);
                mac = macStr;
                break;
            }
            pAdapter = pAdapter->Next;
        }
    }
    free(pAdapterInfo);
    
    return mac;
}

std::string NetworkScanner::PerformOSFingerprint(const std::string& ip) {
    // Simple OS detection based on TTL
    std::string cmd = "ping -n 1 " + ip;
    std::string output;
    char buffer[256];
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "Unknown";
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    _pclose(pipe);
    
    // Parse TTL from output
    std::regex ttl_re(R"(ttl=(\d+))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(output, match, ttl_re)) {
        int ttl = std::stoi(match[1]);
        if (ttl <= 64) return "Linux/Unix";
        else if (ttl <= 128) return "Windows";
        else if (ttl <= 255) return "Cisco/Other";
    }
    
    return "Unknown";
}

std::vector<PortInfo> NetworkScanner::ScanPorts(
    const std::string& target_ip,
    int start_port,
    int end_port,
    int timeout_ms) {
    
    std::vector<PortInfo> results;
    
    // Common services mapping
    std::map<int, std::string> services = {
        {21, "FTP"}, {22, "SSH"}, {23, "Telnet"}, {25, "SMTP"},
        {53, "DNS"}, {80, "HTTP"}, {110, "POP3"}, {143, "IMAP"},
        {443, "HTTPS"}, {445, "SMB"}, {3306, "MySQL"}, {3389, "RDP"},
        {5432, "PostgreSQL"}, {8080, "HTTP-Alt"}
    };
    
    std::vector<std::thread> threads;
    const int MAX_THREADS = 100;
    
    for (int port = start_port; port <= end_port; port++) {
        if (stop_scan) break;
        
        threads.emplace_back([this, &target_ip, port, timeout_ms, &results, &services]() {
            bool open = CheckPort(target_ip, port, timeout_ms);
            
            PortInfo info;
            info.port = port;
            info.is_open = open;
            
            auto it = services.find(port);
            if (it != services.end()) {
                info.service = it->second;
            } else {
                info.service = "unknown";
            }
            
            if (open) {
                info.banner = GetServiceBanner(target_ip, port, timeout_ms);
            }
            
            EnterCriticalSection(&cs);
            results.push_back(info);
            LeaveCriticalSection(&cs);
        });
        
        if (threads.size() >= MAX_THREADS) {
            for (auto& t : threads) {
                if (t.joinable()) t.join();
            }
            threads.clear();
        }
    }
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    
    return results;
}

bool NetworkScanner::CheckPort(const std::string& ip, int port, int timeout_ms) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;
    
    // Set timeout
    int timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server.sin_addr);
    
    int result = connect(sock, (struct sockaddr*)&server, sizeof(server));
    closesocket(sock);
    
    return result == 0;
}

std::string NetworkScanner::GetServiceBanner(const std::string& ip, int port, int timeout_ms) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return "";
    
    // Set timeout
    int timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &server.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
        closesocket(sock);
        return "";
    }
    
    // Send simple probe
    const char* probe = "HEAD / HTTP/1.0\r\n\r\n";
    send(sock, probe, strlen(probe), 0);
    
    char buffer[1024] = {0};
    int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    closesocket(sock);
    
    if (bytes > 0) {
        return std::string(buffer);
    }
    return "";
}

uint32_t NetworkScanner::IPToUInt(const std::string& ip) {
    struct in_addr addr;
    inet_pton(AF_INET, ip.c_str(), &addr);
    return ntohl(addr.s_addr);
}

std::string NetworkScanner::UIntToIP(uint32_t ip) {
    struct in_addr addr;
    addr.s_addr = htonl(ip);
    return std::string(inet_ntoa(addr));
}

std::vector<std::string> NetworkScanner::GetActiveHosts(
    const std::string& subnet,
    int timeout_ms) {
    
    std::vector<std::string> active_hosts;
    auto results = ScanNetwork(subnet, timeout_ms, 50);
    
    for (const auto& device : results) {
        if (device.is_active) {
            active_hosts.push_back(device.ip_address);
        }
    }
    
    return active_hosts;
}

std::string NetworkScanner::ReverseDNS(const std::string& ip) {
    return GetHostname(ip);
}

void NetworkScanner::ScanAsync(const std::string& subnet) {
    if (is_scanning) return;
    
    is_scanning = true;
    stop_scan = false;
    
    scan_thread = std::thread([this, subnet]() {
        ScanNetwork(subnet, 1000, 50);
        is_scanning = false;
    });
}

bool NetworkScanner::IsScanning() const {
    return is_scanning;
}

void NetworkScanner::StopScan() {
    stop_scan = true;
    if (scan_thread.joinable()) {
        scan_thread.join();
    }
    is_scanning = false;
}

std::vector<NetworkDevice> NetworkScanner::GetScanResults() {
    EnterCriticalSection(&cs);
    std::vector<NetworkDevice> temp = devices;
    LeaveCriticalSection(&cs);
    return temp;
}

bool NetworkScanner::IsIPInSubnet(const std::string& ip, const std::string& subnet) {
    // Simplified - just check if IP starts with subnet prefix
    size_t slash = subnet.find('/');
    if (slash == std::string::npos) return ip == subnet;
    
    std::string subnet_ip = subnet.substr(0, slash);
    return ip.find(subnet_ip.substr(0, subnet_ip.find_last_of('.'))) == 0;
}
