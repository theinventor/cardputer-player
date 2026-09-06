#include "audio_player.h"
#include <M5Cardputer.h>
#include <cmath>

namespace ct {
const char* playbackName(Playback value) {
    switch (value) {
    case Playback::Loading: return "loading";
    case Playback::Playing: return "playing";
    case Playback::Paused: return "paused";
    case Playback::Ended: return "ended";
    case Playback::Error: return "error";
    default: return "stopped";
    }
}
bool AudioPlayer::begin(uint8_t volume) {
    working_.volume = std::min<uint8_t>(volume, 100);
    M5Cardputer.Speaker.setVolume(working_.volume * 255 / 100);
    if (!M5Cardputer.Speaker.begin()) return false;
    stream_.keepGoing = keepGoing; stream_.context = this;
    commands_ = xQueueCreate(8, sizeof(AudioCommand));
    publish();
    return commands_ && xTaskCreatePinnedToCore(run, "mp3", 32768, this, 3, nullptr, 0) == pdPASS;
}
bool AudioPlayer::send(AudioCommand command) {
    if (!commands_ || uxQueueSpacesAvailable(commands_) == 0) return false;
    if (command.action == AudioAction::Play || command.action == AudioAction::Stop) command.epoch = ++epoch_;
    return xQueueSend(commands_, &command, 0) == pdTRUE;
}
AudioState AudioPlayer::state() { std::lock_guard<std::mutex> lock(stateMutex_); return public_; }
void AudioPlayer::publish() { std::lock_guard<std::mutex> lock(stateMutex_); public_ = working_; }
void AudioPlayer::run(void* context) { static_cast<AudioPlayer*>(context)->task(); }
bool AudioPlayer::keepGoing(void* context) {
    auto self = static_cast<AudioPlayer*>(context);
    if (self->working_.epoch != self->epoch_.load()) return false;
    if (millis() - self->lastProgress_ >= 50) {
        self->lastProgress_ = millis();
        self->working_.loadingPercent = self->stream_.indexingProgress() * 100;
        self->publish(); delay(1);
    }
    return true;
}
void AudioPlayer::stopOutput() {
    M5Cardputer.Speaker.stop(0);
    // The speaker retains buffer pointers until its worker consumes the stop.
    while (M5Cardputer.Speaker.isPlaying(0)) delay(1);
    buffer_ = 0;
}
void AudioPlayer::handle(const AudioCommand& command) {
    auto& speaker = M5Cardputer.Speaker;
    bool wasPlaying = working_.playback == Playback::Playing;
    switch (command.action) {
    case AudioAction::Play:
        if (command.epoch != epoch_.load()) return;
        stopOutput(); file_.close();
        working_.track = command.track; working_.epoch = command.epoch;
        working_.playback = Playback::Loading; working_.error[0] = 0;
        working_.positionMs = working_.durationMs = working_.loadingPercent = 0;
        publish();
        if (!file_.open(command.path) || !stream_.open(file_)) {
            working_.playback = Playback::Error;
            copyText(working_.error, stream_.error().empty() ? "Cannot open MP3" : stream_.error());
            file_.close(); break;
        }
        working_.rate = stream_.rate(); working_.channels = stream_.channels();
        working_.durationMs = stream_.durationMs();
        if (command.value) stream_.seekMs(command.value);
        working_.positionMs = stream_.positionMs();
        working_.playback = command.paused ? Playback::Paused : Playback::Playing;
        break;
    case AudioAction::Stop:
        stopOutput(); file_.close(); working_.playback = Playback::Stopped;
        working_.epoch = command.epoch; break;
    case AudioAction::Pause:
    case AudioAction::Toggle:
        if (working_.playback == Playback::Playing) {
            stopOutput(); working_.playback = Playback::Paused; break;
        }
        if (command.action != AudioAction::Toggle) break;
        [[fallthrough]];
    case AudioAction::Resume:
        if (working_.playback == Playback::Paused) working_.playback = Playback::Playing;
        break;
    case AudioAction::Seek:
        if (working_.playback == Playback::Playing || working_.playback == Playback::Paused || working_.playback == Playback::Ended) {
            stopOutput();
            if (stream_.seekMs(command.value)) {
                working_.positionMs = stream_.positionMs();
                if (working_.playback == Playback::Ended) working_.playback = Playback::Paused;
            }
        }
        break;
    case AudioAction::Volume:
        working_.volume = std::min<uint32_t>(command.value, 100);
        speaker.setVolume(working_.volume * 255 / 100); break;
    }
    publish();
    if (command.action == AudioAction::Pause && command.value) {
        pauseWasPlaying_.store(wasPlaying);
        pauseDone_.store(command.value);
    }
}
void AudioPlayer::spectrum(const int16_t* pcm, int count) {
    if (!visualize_.load() || millis() - lastSpectrum_ < 100 || count < 256 * working_.channels) return;
    lastSpectrum_ = millis();
    // A small logarithmic Goertzel bank measures actual PCM energy. Computing
    // only the displayed bands costs less than a full spectrum on this MCU.
    for (int band = 0; band < 16; ++band) {
        float hz = 100.0f * powf(1.36f, band);
        float coefficient = 2 * cosf(2 * float(M_PI) * hz / working_.rate);
        float previous = 0, previous2 = 0;
        for (int i = 0; i < 256; ++i) {
            float sample = pcm[i * working_.channels];
            if (working_.channels == 2) sample = (sample + pcm[i * 2 + 1]) / 2;
            sample *= 0.5f - 0.5f * cosf(2 * float(M_PI) * i / 255);
            float next = sample + coefficient * previous - previous2;
            previous2 = previous; previous = next;
        }
        float power = previous * previous + previous2 * previous2 - coefficient * previous * previous2;
        float db = 20 * log10f(sqrtf(std::max(0.0f, power)) / (128 * 32768) + 1e-6f);
        int value = std::max(0, std::min(100, int((db + 65) * 100 / 65)));
        working_.bands[band] = std::max(value, int(working_.bands[band]) - 8);
    }
}
void AudioPlayer::task() {
    while (true) {
        AudioCommand command;
        while (xQueueReceive(commands_, &command, 0) == pdTRUE) handle(command);
        if (working_.playback == Playback::Playing) {
            if (M5Cardputer.Speaker.isPlaying(0) < 2) {
                int count = stream_.decode(pcm_[buffer_]);
                if (count > 0) {
                    spectrum(pcm_[buffer_], count);
                    M5Cardputer.Speaker.playRaw(pcm_[buffer_], count, stream_.rate(), stream_.channels() == 2, 1, 0);
                    buffer_ = (buffer_ + 1) % 3;
                    working_.positionMs = stream_.positionMs(); publish();
                } else {
                    while (M5Cardputer.Speaker.isPlaying(0) && uxQueueMessagesWaiting(commands_) == 0) delay(1);
                    working_.playback = stream_.error().empty() ? Playback::Ended : Playback::Error;
                    copyText(working_.error, stream_.error()); publish();
                }
            }
        }
        delay(1);
    }
}
bool AudioPlayer::pauseAndWait(bool* wasPlaying) {
    AudioCommand command; command.action = AudioAction::Pause;
    if (++pauseRequest_ == 0) ++pauseRequest_;
    command.value = pauseRequest_;
    if (!send(command)) return false;
    for (unsigned i = 0; i < 2000; ++i) {
        if (pauseDone_.load() == command.value) {
            if (wasPlaying) *wasPlaying = pauseWasPlaying_.load();
            return true;
        }
        delay(1);
    }
    return false;
}
bool AudioPlayer::stopAndWait() {
    AudioCommand command; command.action = AudioAction::Stop;
    if (!send(command)) return false;
    auto desired = epoch_.load();
    for (unsigned i = 0; i < 2000; ++i) {
        auto s = state();
        if (s.epoch == desired && s.playback == Playback::Stopped) return true;
        delay(1);
    }
    return false;
}
}
