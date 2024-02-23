#ifndef KLARITY_AUDIO_AUDIO_H
#define KLARITY_AUDIO_AUDIO_H

#include <mutex>
#include <vector>
#include <memory>
#include "openal/alc.h"
#include "openal/al.h"
#include "stretch/stretch.h"

class IAudio {
public:
    virtual ~IAudio() = default;

    virtual void setPlaybackSpeed(float factor) = 0;

    virtual bool setVolume(float value) = 0;

    virtual bool play(uint8_t *samples, uint64_t size) = 0;

    virtual void pause() = 0;

    virtual void resume() = 0;

    virtual void stop() = 0;
};

class Audio : public IAudio {
private:
    std::mutex mutex;
    const int MIN_BUFFERS_NUM = 3;
    ALenum format = AL_NONE;
    float playbackSpeedFactor = 1.0f;
    uint32_t source;
    uint32_t sampleRate;
    ALCdevice *device;
    ALCcontext *context;
    Stretch<float> *stretch;

    static std::vector<uint8_t> _convertSamples(float *samples, uint64_t size);

    static void _checkALError(const char *file, int line);

    static ALenum _getFormat(uint32_t bitsPerSample, uint32_t channels);

    void _discardQueuedBuffers() const;

    void _discardProcessedBuffers() const;

    void _cleanUp();

public:
    explicit Audio(uint32_t bitsPerSample, uint32_t sampleRate, uint32_t channels);

    ~Audio() override;

    void setPlaybackSpeed(float factor) override;

    bool setVolume(float value) override;

    bool play(uint8_t *samples, uint64_t size) override;

    void pause() override;

    void resume() override;

    void stop() override;
};

#endif //KLARITY_AUDIO_AUDIO_H
