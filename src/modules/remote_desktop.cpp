#include "remote_desktop.h"
#include "../utils/logger.h"
#include <vector>
#include <sstream>
#include <iostream>
#include <cstring>

// WIC for JPEG encoding
#include <wincodec.h>
#include <objbase.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// ------------------------------------------------------------------
// Encode a top-down 32-bit BGRA buffer as JPEG using WIC.
// ------------------------------------------------------------------
static bool encode_jpeg_wic(const uint8_t* bgra,
                            UINT32 width,
                            UINT32 height,
                            UINT32 stride,
                            int quality,
                            std::vector<uint8_t>& out) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICStream* stream = nullptr;
    IStream* memStream = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &memStream))) goto cleanup;
    if (FAILED(factory->CreateStream(&stream))) goto cleanup;
    if (FAILED(stream->InitializeFromIStream(memStream))) goto cleanup;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &encoder))) goto cleanup;
    if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) goto cleanup;
    if (FAILED(encoder->CreateNewFrame(&frame, &props))) goto cleanup;

    {
        PROPBAG2 opt = {0};
        wchar_t quality_name[] = L"ImageQuality";
        opt.pstrName = quality_name;
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = (float)quality / 100.0f;
        props->Write(1, &opt, &v);
    }

    if (FAILED(frame->Initialize(props))) goto cleanup;
    if (FAILED(frame->SetSize(width, height))) goto cleanup;

    {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&fmt))) goto cleanup;
    }

    if (FAILED(frame->WritePixels(height, stride, stride * height,
                                  (BYTE*)bgra))) goto cleanup;
    if (FAILED(frame->Commit())) goto cleanup;
    if (FAILED(encoder->Commit())) goto cleanup;

    {
        STATSTG stat = {0};
        memStream->Stat(&stat, STATFLAG_NONAME);
        ULARGE_INTEGER size = stat.cbSize;
        if (size.QuadPart == 0 || size.QuadPart > 20 * 1024 * 1024) goto cleanup;

        LARGE_INTEGER zero = {0};
        memStream->Seek(zero, STREAM_SEEK_SET, NULL);

        out.resize((size_t)size.QuadPart);
        ULONG read = 0;
        memStream->Read(out.data(), (ULONG)out.size(), &read);
        out.resize(read);
        ok = true;
    }

cleanup:
    if (props) props->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (memStream) memStream->Release();
    if (factory) factory->Release();
    return ok;
}

RemoteDesktop::RemoteDesktop()
    : streaming(false), quality(70), fps(10), compression("jpeg") {
    screen_width  = GetSystemMetrics(SM_CXSCREEN);
    screen_height = GetSystemMetrics(SM_CYSCREEN);
    Logger::getInstance().log("RemoteDesktop: " +
        std::to_string(screen_width) + "x" + std::to_string(screen_height));
}

RemoteDesktop::~RemoteDesktop() {
    stop_streaming();
}

// ------------------------------------------------------------------
// Capture the desktop as a top-down 32-bit BGRA buffer, then encode to JPEG.
// ------------------------------------------------------------------
std::vector<uint8_t> RemoteDesktop::capture_bmp() {
    std::vector<uint8_t> result;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, screen_width, screen_height);
    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hBitmap);

    BitBlt(hdcMem, 0, 0, screen_width, screen_height, hdcScreen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = screen_width;
    bi.biHeight      = -screen_height;   // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    UINT32 stride = screen_width * 4;
    result.resize(stride * screen_height);

    if (GetDIBits(hdcScreen, hBitmap, 0, screen_height,
                  result.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS) == 0) {
        result.clear();
    }

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return result;
}

std::vector<uint8_t> RemoteDesktop::bmp_to_jpeg(const std::vector<uint8_t>& bgra,
                                                 int q) {
    std::vector<uint8_t> jpeg;
    if (bgra.empty()) return jpeg;
    UINT32 stride = screen_width * 4;
    if (!encode_jpeg_wic(bgra.data(), screen_width, screen_height,
                         stride, q, jpeg)) {
        jpeg.clear();
    }
    return jpeg;
}

std::vector<uint8_t> RemoteDesktop::capture_frame() {
    std::vector<uint8_t> bgra = capture_bmp();
    if (bgra.empty()) return {};
    return bmp_to_jpeg(bgra, quality);
}

void RemoteDesktop::stream_loop() {
    auto frame_duration = std::chrono::milliseconds(1000 / fps);

    while (streaming.load()) {
        auto start = std::chrono::high_resolution_clock::now();

        std::vector<uint8_t> jpeg = capture_frame();
        if (!jpeg.empty() && callback) {
            callback(jpeg);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }
}

bool RemoteDesktop::start_streaming(std::function<void(const std::vector<uint8_t>&)> cb) {
    if (streaming.load()) return false;
    callback = cb;
    streaming.store(true);
    stream_thread = std::thread(&RemoteDesktop::stream_loop, this);
    Logger::getInstance().log("RemoteDesktop: streaming started");
    return true;
}

void RemoteDesktop::stop_streaming() {
    if (!streaming.load()) return;
    streaming.store(false);
    if (stream_thread.joinable()) stream_thread.join();
    callback = nullptr;
    Logger::getInstance().log("RemoteDesktop: streaming stopped");
}

bool RemoteDesktop::set_quality(int q) {
    if (q < 1 || q > 100) return false;
    quality = q;
    return true;
}

bool RemoteDesktop::set_fps(int f) {
    if (f < 1 || f > 60) return false;
    fps = f;
    return true;
}

bool RemoteDesktop::set_compression(const std::string& method) {
    compression = method;
    return true;
}

void RemoteDesktop::get_screen_info(int& width, int& height) {
    width = screen_width;
    height = screen_height;
}