#include "media.h"

#define CHECK_AL_ERROR() _checkALError(__FILE__, __LINE__)

void Media::_checkALError(const char *file, int line) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << " - " << alGetString(error) << std::endl;
    }
}

Media::Media(uint32_t sampleRate, uint32_t channels, uint32_t numBuffers) {
    this->sampleRate = sampleRate;

    this->channels = channels;

    this->numBuffers = numBuffers;

    this->format = (channels == 1) ? AL_FORMAT_MONO_FLOAT32 : (channels == 2) ? AL_FORMAT_STEREO_FLOAT32 : AL_NONE;

    if (this->format == AL_NONE) {
        std::cerr << "Unsupported audio format." << std::endl;
    }

    alGenSources(1, &source);

    this->stretch = new signalsmith::stretch::SignalsmithStretch<float>();

    this->stretch->presetDefault((int) this->channels, (float) this->sampleRate);
}

Media::~Media() {
    alDeleteSources(1, &this->source);
    this->stretch->reset();
    delete this->stretch;
}

void Media::changePlaybackSpeed(float factor) {
    this->playbackSpeedFactor = factor;
}