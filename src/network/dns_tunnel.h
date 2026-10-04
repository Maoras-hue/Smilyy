#ifndef DNS_TUNNEL_H
#define DNS_TUNNEL_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <map>
#include <queue>
#include <mutex>

class DNSTunnel {
public:
    DNSTunnel();
    ~DNSTunnel();

    bool StartTunnel(const std::string& server_domain, int max_packet_size = 255);
    bool StopTunnel();
    bool IsRunning() const;

    bool SendData(const std::string& data, const std::string& target = "");
    std::string ReceiveData(int timeout_ms = 5000);
    bool SendFile(const std::string& file_path, const std::string& target = "");
    bool ReceiveFile(const std::string& save_path);

    std::string EncodeData(const std::string& data);
    std::string DecodeData(const std::string& encoded);

    void SetDnsServer(const std::string& dns_server);
    void SetTtl(int ttl);
    void SetRecursion(bool enable);

private:
    std::thread dns_thread;
    std::atomic<bool> is_running;
    std::queue<std::string> receive_queue;
    std::queue<std::string> send_queue;
    mutable std::mutex queue_mutex;
    
    std::string server_domain;
    std::string dns_server;
    int max_packet_size;
    int ttl;
    bool recursion_enabled;
    
    // Session management
    std::map<std::string, std::string> sessions;
    std::string current_session_id;

    void DNSServerLoop();
    void ProcessSendQueue();
    std::vector<uint8_t> BuildDNSQuery(const std::string& domain, const std::string& data);
    bool SendDNSQuery(const std::vector<uint8_t>& query);
    std::vector<uint8_t> ReceiveDNSResponse();
    std::string ExtractDataFromDNS(const std::vector<uint8_t>& response);
    
    std::string EncodeBase64(const std::string& data);
    std::string DecodeBase64(const std::string& data);
    std::string EncodeHex(const std::string& data);
    std::string DecodeHex(const std::string& data);
    
    std::string GenerateSessionID();
    bool ResolveDomain(const std::string& domain, std::string& ip);
};

#endif // DNS_TUNNEL_H
