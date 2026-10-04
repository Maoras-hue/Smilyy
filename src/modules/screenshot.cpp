#include "screenshot.h"
#include "../utils/logger.h"
#include <fstream>
#include <iostream>

std::vector<uint8_t> ScreenshotModule::capture() {
    std::vector<uint8_t> result;
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screen_width, screen_height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, screen_width, screen_height, hdcScreen, 0, 0, SRCCOPY);
    
    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER) };
    bi.biWidth = screen_width;
    bi.biHeight = -screen_height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    
    int stride = ((screen_width * 24 + 31) / 32) * 4;
    int image_size = stride * screen_height;
    result.resize(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size);
    
    BITMAPFILEHEADER bf;
    bf.bfType = 0x4D42;
    bf.bfSize = result.size();
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfReserved1 = bf.bfReserved2 = 0;
    
    memcpy(result.data(), &bf, sizeof(BITMAPFILEHEADER));
    memcpy(result.data() + sizeof(BITMAPFILEHEADER), &bi, sizeof(BITMAPINFOHEADER));
    
    GetDIBits(hdcScreen, hBitmap, 0, screen_height, 
              result.data() + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER),
              (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    
    Logger::getInstance().log("Screenshot captured: " + std::to_string(result.size()) + " bytes");
    return result;
}

bool ScreenshotModule::save_to_file(const std::string& path) {
    auto data = capture();
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    Logger::getInstance().log("Screenshot saved: " + path);
    return true;
}

std::vector<uint8_t> ScreenshotModule::capture_region(int x, int y, int width, int height) {
    std::vector<uint8_t> result;
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, x, y, SRCCOPY);
    
    BITMAPINFOHEADER bi = { sizeof(BITMAPINFOHEADER) };
    bi.biWidth = width;
    bi.biHeight = -height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    
    int stride = ((width * 24 + 31) / 32) * 4;
    int image_size = stride * height;
    result.resize(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size);
    
    BITMAPFILEHEADER bf;
    bf.bfType = 0x4D42;
    bf.bfSize = result.size();
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfReserved1 = bf.bfReserved2 = 0;
    
    memcpy(result.data(), &bf, sizeof(BITMAPFILEHEADER));
    memcpy(result.data() + sizeof(BITMAPFILEHEADER), &bi, sizeof(BITMAPINFOHEADER));
    
    GetDIBits(hdcScreen, hBitmap, 0, height,
              result.data() + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER),
              (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return result;
}

std::vector<uint8_t> ScreenshotModule::capture_window(HWND hWnd) {
    RECT rect;
    GetWindowRect(hWnd, &rect);
    return capture_region(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

std::vector<uint8_t> ScreenshotModule::encode_jpeg(const std::vector<uint8_t>& bmp_data, int quality) {
    // Placeholder - would need libjpeg or Windows Imaging Component
    return bmp_data;
}
