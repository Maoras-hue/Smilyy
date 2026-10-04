#ifndef NETWORK_SCANNER_H
#define NETWORK_SCANNER_H

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <map>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

struct NetworkDevice {
    std::string ip_address;
    std::string mac_address;
    std::string hostname;
    std::string operating_system;
    std::vector<int> open_ports;
    bool is_active;
    int response_time_ms;
};

struct PortInfo {
    int port;
    std::string service;
    bool is_open;
    std::string banner;
};

class NetworkScanner {
public:
    NetworkScanner();
    ~NetworkScanner();

    std::vector<NetworkDevice> ScanNetwork(
        const std::string& subnet,
        int timeout_ms = 1000,
        int max_threads = 100
    );

    std::vector<PortInfo> ScanPorts(
        const std::string& target_ip,
        int start_port = 1,
        int end_port = 65535,
        int timeout_ms = 500
    );

    std::vector<std::string> GetActiveHosts(
        const std::string& subnet,
        int timeout_ms = 1000
    );

    bool PingHost(const std::string& ip, int timeout_ms = 1000);
    std::string GetHostname(const std::string& ip);
    std::string ReverseDNS(const std::string& ip);

    void ScanAsync(const std::string& subnet);
    bool IsScanning() const;
    void StopScan();
    std::vector<NetworkDevice> GetScanResults();

private:
    std::thread scan_thread;
    std::atomic<bool> is_scanning;
    std::atomic<bool> stop_scan;
    std::vector<NetworkDevice> devices;
    mutable CRITICAL_SECTION cs;

    bool IsIPInSubnet(const std::string& ip, const std::string& subnet);
    std::vector<std::string> GetIPsInSubnet(const std::string& subnet);
    std::string GetMACAddress(const std::string& ip);
    std::string PerformOSFingerprint(const std::string& ip);
    bool CheckPort(const std::string& ip, int port, int timeout_ms);
    std::string GetServiceBanner(const std::string& ip, int port, int timeout_ms);
    uint32_t IPToUInt(const std::string& ip);
    std::string UIntToIP(uint32_t ip);
};

#endif // NETWORK_SCANNER_H
