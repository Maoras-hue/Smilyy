#include "webcam.h"
#include "../utils/logger.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <comdef.h>
#include <objbase.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#include <vector>

// ------------------------------------------------------------------
// JPEG encoder for a single IMFMediaBuffer (BGRA or YUY2) -> bytes
// We do the simplest possible path: convert to RGB32, then use
// Windows Imaging Component (WIC) to encode as JPEG.
// ------------------------------------------------------------------
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

static bool EncodeToJpeg(const BYTE* pixels,
                         UINT32 width,
                         UINT32 height,
                         UINT32 stride,
                         int quality,
                         std::vector<uint8_t>& out) {
    // pixels must be 32-bit BGRA (format GUID_WICPixelFormat32bppBGRA).

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    IWICBitmapEncoder* encoder = nullptr;
    IWICStream* stream = nullptr;
    IStream* memStream = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;

    // Create an in-memory stream
    if (FAILED(hr = CreateStreamOnHGlobal(NULL, TRUE, &memStream))) goto cleanup;
    if (FAILED(hr = factory->CreateStream(&stream))) goto cleanup;
    if (FAILED(hr = stream->InitializeFromIStream(memStream))) goto cleanup;
    if (FAILED(hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &encoder))) goto cleanup;
    if (FAILED(hr = encoder->Initialize(stream, WICBitmapEncoderNoCache))) goto cleanup;
    if (FAILED(hr = encoder->CreateNewFrame(&frame, &props))) goto cleanup;

    // Quality property
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

    if (FAILED(frame->WritePixels(
            height, stride, stride * height,
            (BYTE*)pixels))) goto cleanup;

    if (FAILED(hr = frame->Commit())) goto cleanup;
    if (FAILED(hr = encoder->Commit())) goto cleanup;

    // Read bytes from memStream
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
    }

cleanup:
    if (props) props->Release();
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (memStream) memStream->Release();
    if (factory) factory->Release();
    return hr == S_OK;
}

WebcamModule::WebcamModule() : com_initialized(false), mf_initialized(false) {}
WebcamModule::~WebcamModule() { shutdown(); }

bool WebcamModule::initialize() {
    if (!com_initialized) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            com_initialized = true;
        } else {
            Logger::getInstance().log("Webcam: CoInitializeEx failed.");
            return false;
        }
    }
    if (!mf_initialized) {
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            Logger::getInstance().log("Webcam: MFStartup failed.");
            return false;
        }
        mf_initialized = true;
    }
    return true;
}

void WebcamModule::shutdown() {
    if (mf_initialized) {
        MFShutdown();
        mf_initialized = false;
    }
    if (com_initialized) {
        CoUninitialize();
        com_initialized = false;
    }
}

bool WebcamModule::is_available() {
    if (!initialize()) return false;

    IMFAttributes* attrs = nullptr;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;

    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (FAILED(hr)) return false;

    hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { attrs->Release(); return false; }

    hr = MFEnumDeviceSources(attrs, &devices, &count);
    attrs->Release();

    bool ok = SUCCEEDED(hr) && count > 0;

    if (devices) {
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
    }
    return ok;
}

std::string WebcamModule::get_device_name() {
    if (!initialize()) return "none";

    IMFAttributes* attrs = nullptr;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    std::string result = "none";

    if (FAILED(MFCreateAttributes(&attrs, 1))) return result;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    if (SUCCEEDED(MFEnumDeviceSources(attrs, &devices, &count)) && count > 0) {
        WCHAR* name = nullptr;
        UINT32 name_len = 0;
        if (SUCCEEDED(devices[0]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len))) {
            char buf[256] = {0};
            WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, sizeof(buf) - 1, NULL, NULL);
            result = buf;
            CoTaskMemFree(name);
        }
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
    }
    attrs->Release();
    return result;
}

// Internal helper: open the first camera and read one sample as 32-bit BGRA.
static bool CaptureOneBgra(std::vector<uint8_t>& out_bgra,
                           UINT32& width, UINT32& height) {
    IMFAttributes* attrs = nullptr;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    IMFMediaSource* source = nullptr;
    IMFSourceReader* reader = nullptr;
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buffer = nullptr;
    IMFMediaType* outType = nullptr;
    bool ok = false;

    if (FAILED(MFCreateAttributes(&attrs, 1))) return false;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(MFEnumDeviceSources(attrs, &devices, &count)) || count == 0) {
        attrs->Release();
        return false;
    }
    attrs->Release();

    if (FAILED(devices[0]->ActivateObject(IID_PPV_ARGS(&source)))) {
        for (UINT32 i = 0; i < count; i++) devices[i]->Release();
        CoTaskMemFree(devices);
        return false;
    }
    for (UINT32 i = 0; i < count; i++) devices[i]->Release();
    CoTaskMemFree(devices);

    if (FAILED(MFCreateSourceReaderFromMediaSource(source, NULL, &reader))) goto cleanup;

    if (FAILED(MFCreateMediaType(&outType))) goto cleanup;
    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(reader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, outType))) goto cleanup;

    {
        DWORD streamIndex = 0, flags = 0;
        LONGLONG timestamp = 0;
        HRESULT hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &streamIndex, &flags, &timestamp, &sample);
        if (FAILED(hr) || !sample) goto cleanup;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) goto cleanup;
    }

    // Get frame format for stride/size
    {
        IMFMediaType* current = nullptr;
        if (FAILED(reader->GetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current))) goto cleanup;

        UINT32 w = 0, h = 0;
        MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
        current->Release();
        width = w;
        height = h;
    }

    if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) goto cleanup;

    {
        BYTE* data = nullptr;
        DWORD maxLen = 0, curLen = 0;
        if (FAILED(buffer->Lock(&data, &maxLen, &curLen))) goto cleanup;
        out_bgra.assign(data, data + curLen);
        buffer->Unlock();
    }

    ok = true;

cleanup:
    if (outType) outType->Release();
    if (buffer) buffer->Release();
    if (sample) sample->Release();
    if (reader) reader->Release();
    if (source) source->Release();
    return ok;
}

bool WebcamModule::capture_frame(std::vector<uint8_t>& image_data, int quality) {
    if (!initialize()) return false;

    std::vector<uint8_t> bgra;
    UINT32 w = 0, h = 0;
    if (!CaptureOneBgra(bgra, w, h)) {
        Logger::getInstance().log("Webcam: capture failed (no device or read error).");
        return false;
    }

    // MFVideoFormat_RGB32 is BGRA in memory (bottom-up by default).
    // WIC wants top-down. Flip rows.
    UINT32 stride = w * 4;
    std::vector<uint8_t> flipped(bgra.size());
    for (UINT32 y = 0; y < h; y++) {
        memcpy(&flipped[y * stride],
               &bgra[(h - 1 - y) * stride],
               stride);
    }

    bool ok = EncodeToJpeg(flipped.data(), w, h, stride, quality, image_data);
    if (ok) {
        Logger::getInstance().log("Webcam: captured frame, " +
            std::to_string(image_data.size()) + " bytes JPEG");
    }
    return ok;
}

bool WebcamModule::capture_sequence(std::vector<std::vector<uint8_t>>& frames,
                                    int count, int delay_ms, int quality) {
    frames.clear();
    for (int i = 0; i < count; i++) {
        std::vector<uint8_t> frame;
        if (capture_frame(frame, quality)) frames.push_back(std::move(frame));
        Sleep(delay_ms);
    }
    return !frames.empty();
}

bool WebcamModule::start_recording(const std::string& path, int duration_seconds) {
    // Placeholder — real MP4 recording requires the Sink Writer pipeline.
    // For now, use capture_sequence and stitch frames on the server side.
    (void)path; (void)duration_seconds;
    return false;
}