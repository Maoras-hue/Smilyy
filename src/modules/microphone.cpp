#include "microphone.h"
#include "../utils/logger.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <comdef.h>
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#include <fstream>
#include <cstring>

MicrophoneModule::MicrophoneModule() : com_initialized(false) {}
MicrophoneModule::~MicrophoneModule() { shutdown(); }

bool MicrophoneModule::initialize() {
    if (!com_initialized) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            com_initialized = true;
        } else {
            Logger::getInstance().log("Microphone: CoInitializeEx failed.");
            return false;
        }
    }
    return true;
}

void MicrophoneModule::shutdown() {
    if (com_initialized) {
        CoUninitialize();
        com_initialized = false;
    }
}

bool MicrophoneModule::is_available() {
    if (!initialize()) return false;

    IMMDeviceEnumerator* enumr = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        IID_PPV_ARGS(&enumr));
    if (FAILED(hr)) return false;

    IMMDevice* dev = nullptr;
    hr = enumr->GetDefaultAudioEndpoint(eCapture, eConsole, &dev);
    if (dev) dev->Release();
    enumr->Release();
    return SUCCEEDED(hr);
}

// ---- WAV header helper ----
static void write_le16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
}
static void write_le32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 24) & 0xFF));
}

static std::vector<uint8_t> make_wav_header(uint32_t data_bytes,
                                            uint16_t channels,
                                            uint32_t sample_rate,
                                            uint16_t bits_per_sample) {
    std::vector<uint8_t> h;
    uint16_t block_align = channels * (bits_per_sample / 8);
    uint32_t byte_rate = sample_rate * block_align;

    // RIFF
    h.push_back('R'); h.push_back('I'); h.push_back('F'); h.push_back('F');
    write_le32(h, 36 + data_bytes);
    h.push_back('W'); h.push_back('A'); h.push_back('V'); h.push_back('E');

    // fmt
    h.push_back('f'); h.push_back('m'); h.push_back('t'); h.push_back(' ');
    write_le32(h, 16);
    write_le16(h, 1);                    // PCM
    write_le16(h, channels);
    write_le32(h, sample_rate);
    write_le32(h, byte_rate);
    write_le16(h, block_align);
    write_le16(h, bits_per_sample);

    // data
    h.push_back('d'); h.push_back('a'); h.push_back('t'); h.push_back('a');
    write_le32(h, data_bytes);
    return h;
}

bool MicrophoneModule::record_wav(std::vector<uint8_t>& wav_data, int seconds) {
    wav_data.clear();
    if (!initialize()) return false;
    if (seconds <= 0 || seconds > 60) seconds = 5;

    IMMDeviceEnumerator* enumr = nullptr;
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* mix_format = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        IID_PPV_ARGS(&enumr));
    if (FAILED(hr)) goto cleanup;

    if (FAILED(enumr->GetDefaultAudioEndpoint(eCapture, eConsole, &dev))) goto cleanup;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                             (void**)&client))) goto cleanup;

    if (FAILED(client->GetMixFormat(&mix_format))) goto cleanup;

    // We'll capture in the device's native format for simplicity.
    if (FAILED(client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,                 // no flags
            10000000,          // buffer duration 1s
            0, mix_format, NULL))) goto cleanup;

    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                  (void**)&capture))) goto cleanup;

    if (FAILED(client->Start())) goto cleanup;

    {
        std::vector<uint8_t> pcm;
        uint64_t target_frames = (uint64_t)mix_format->nSamplesPerSec * seconds;

        while (true) {
            UINT32 packet_frames = 0;
            hr = capture->GetNextPacketSize(&packet_frames);
            if (FAILED(hr)) break;

            if (packet_frames == 0) {
                Sleep(10);
                continue;
            }

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, NULL, NULL);
            if (FAILED(hr)) break;

            if (frames > 0) {
                UINT32 bytes = frames * mix_format->nBlockAlign;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                    pcm.insert(pcm.end(), data, data + bytes);
                } else {
                    // silent — append zeros
                    pcm.insert(pcm.end(), bytes, 0);
                }
            }
            capture->ReleaseBuffer(frames);

            uint64_t got_frames = pcm.size() / mix_format->nBlockAlign;
            if (got_frames >= target_frames) break;
        }

        // Trim to exact length
        size_t target_bytes = (size_t)target_frames * mix_format->nBlockAlign;
        if (pcm.size() > target_bytes) pcm.resize(target_bytes);

        // Build WAV
        wav_data = make_wav_header(
            (uint32_t)pcm.size(),
            mix_format->nChannels,
            mix_format->nSamplesPerSec,
            mix_format->wBitsPerSample);
        wav_data.insert(wav_data.end(), pcm.begin(), pcm.end());
        ok = true;
    }

cleanup:
    if (client) client->Stop();
    if (capture) capture->Release();
    if (client) client->Release();
    if (dev) dev->Release();
    if (enumr) enumr->Release();
    if (mix_format) CoTaskMemFree(mix_format);

    if (ok) {
        Logger::getInstance().log("Microphone: recorded " +
            std::to_string(wav_data.size()) + " bytes WAV");
    } else {
        Logger::getInstance().log("Microphone: recording failed.");
    }
    return ok;
}

bool MicrophoneModule::capture_audio(std::vector<uint8_t>& audio_data) {
    return record_wav(audio_data, 3);
}

bool MicrophoneModule::start_recording(const std::string& path, int duration_seconds) {
    std::vector<uint8_t> wav;
    if (!record_wav(wav, duration_seconds)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)wav.data(), (std::streamsize)wav.size());
    return true;
}