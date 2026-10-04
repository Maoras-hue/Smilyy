#ifndef REMOTE_DESKTOP_H
#define REMOTE_DESKTOP_H

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

class RemoteDesktop {
private:
    std::atomic<bool> streaming;
    std::thread stream_thread;
    int screen_width;
    int screen_height;
    int quality;      // JPEG quality 1-100
    int fps;          // frames per second for streaming
    std::string compression;
    std::function<void(const std::vector<uint8_t>&)> callback;

    std::vector<uint8_t> capture_bmp();
    std::vector<uint8_t> bmp_to_jpeg(const std::vector<uint8_t>& bmp, int quality);
    void stream_loop();

public:
    RemoteDesktop();
    ~RemoteDesktop();

    // Capture a single frame, encoded as JPEG. Returns the JPEG bytes.
    std::vector<uint8_t> capture_frame();

    bool start_streaming(std::function<void(const std::vector<uint8_t>&)> cb);
    void stop_streaming();

    bool set_quality(int q);
    bool set_fps(int f);
    bool set_compression(const std::string& method);
    void get_screen_info(int& width, int& height);
    bool is_streaming() const { return streaming.load(); }
};

#endif