#ifndef WEBCAM_H
#define WEBCAM_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

class WebcamModule {
public:
    WebcamModule();
    ~WebcamModule();

    // Initialize COM + Media Foundation. Safe to call multiple times.
    bool initialize();
    void shutdown();

    // Capture one JPEG frame. Returns true on success and fills image_data.
    // Quality: 1-100, JPEG compression quality.
    bool capture_frame(std::vector<uint8_t>& image_data, int quality = 80);

    // Capture N frames at a rough interval. Blocks until done.
    // Useful for a short video clip (each frame is a separate JPEG).
    bool capture_sequence(std::vector<std::vector<uint8_t>>& frames,
                          int count, int delay_ms = 200, int quality = 80);

    // Record to a .mp4 file (uses Media Foundation Sink Writer).
    // Not implemented — reserved for future.
    bool start_recording(const std::string& path, int duration_seconds);

    bool is_available();
    std::string get_device_name();

private:
    bool com_initialized;
    bool mf_initialized;
};

#endif