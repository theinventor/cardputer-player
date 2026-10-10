#pragma once
#include "storage.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace ct {
enum class Playback { Stopped, Loading, Playing, Paused, Ended, Error };
const char* playbackName(Playback value);
struct AudioState {
    Playback playback = Playback::Stopped;
    int track = -1;
    uint32_t epoch = 0, positionMs = 0, durationMs = 0;
    int rate = 0, channels = 0;
    uint8_t volume = 20, loadingPercent = 0;
    uint8_t bands[16]{};
    char error[96]{};
};
enum class AudioAction { Play, Pause, Resume, Toggle, Seek, Stop, Volume, Release };
struct AudioCommand {
    AudioAction action = AudioAction::Stop;
    int track = -1;
    uint32_t value = 0, epoch = 0;
    bool paused = false;
    char path[192]{};
};
class AudioPlayer {
public:
    bool begin(uint8_t volume);
    bool send(AudioCommand command);
    AudioState state();
    uint32_t stackFree() const { return worker_ ? uxTaskGetStackHighWaterMark(worker_) : 0; }
    bool pauseAndWait(bool* wasPlaying = nullptr);
    bool stopAndWait();
    bool releaseAndWait();
    void visualizer(bool enabled) { visualize_.store(enabled); }
private:
    static void run(void* context);
    static bool keepGoing(void* context);
    void task();
    void publish();
    void handle(const AudioCommand& command);
    void stopOutput();
    void spectrum(const int16_t* pcm, int count);
    std::mutex stateMutex_;
    AudioState working_, public_;
    QueueHandle_t commands_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    SDReader file_;
    struct Decoder {
        Mp3Stream stream;
        int16_t pcm[3][MINIMP3_MAX_SAMPLES_PER_FRAME]{};
    };
    std::unique_ptr<Decoder> decoder_;
    std::atomic<uint32_t> epoch_{0};
    std::atomic<bool> visualize_{true};
    std::atomic<uint32_t> pauseDone_{0};
    std::atomic<bool> pauseWasPlaying_{false};
    std::atomic<bool> released_{false};
    uint32_t pauseRequest_ = 0;
    uint32_t lastProgress_ = 0, lastSpectrum_ = 0;
    unsigned buffer_ = 0;
};
}
