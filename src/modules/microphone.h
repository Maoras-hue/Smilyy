#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

class MicrophoneModule {
public:
    MicrophoneModule();
    ~MicrophoneModule();

    bool initialize();
    void shutdown();

    // Record N seconds of audio. Output is a complete WAV file (PCM 16-bit mono 16kHz).
    bool record_wav(std::vector<uint8_t>& wav_data, int seconds = 5);

    // Legacy API — capture_audio now delegates to record_wav.
    bool capture_audio(std::vector<uint8_t>& audio_data);

    // Legacy API — kept for compatibility. Records to the given path.
    bool start_recording(const std::string& path, int duration_seconds);

    bool is_available();

private:
    bool com_initialized;
};

#endif