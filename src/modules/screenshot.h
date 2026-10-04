#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <vector>
#include <cstdint>  // Added
#include <string>
#include <windows.h>

class ScreenshotModule {
public:
    std::vector<uint8_t> capture();
    bool save_to_file(const std::string& path);
    std::vector<uint8_t> capture_region(int x, int y, int width, int height);
    std::vector<uint8_t> capture_window(HWND hWnd);
private:
    std::vector<uint8_t> encode_jpeg(const std::vector<uint8_t>& bmp_data, int quality = 80);
};

#endif
