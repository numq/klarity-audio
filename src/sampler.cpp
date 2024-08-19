#include "sampler.h"

Media *Sampler::_acquireMedia(int64_t id) {
    auto it = mediaPool.find(id);
    if (it == mediaPool.end()) {
        throw MediaNotFoundException();
    }
    return it->second;
}

void Sampler::_releaseMedia(int64_t id) {
    auto it = mediaPool.find(id);
    if (it == mediaPool.end()) {
        throw MediaNotFoundException();
    }

    delete it->second;
    mediaPool.erase(it);
}

Sampler::Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw SamplerException("Failed to initialize PortAudio: " + std::string(Pa_GetErrorText(err)));
    }
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &media: mediaPool) {
        _releaseMedia(media.first);
    }

    Pa_Terminate();
}

void Sampler::setPlaybackSpeed(int64_t id, float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->setPlaybackSpeed(factor);
}

void Sampler::setVolume(int64_t id, float value) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->setVolume(value);
}

void Sampler::initialize(int64_t id, uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    if (mediaPool.find(id) != mediaPool.end()) {
        throw SamplerException("Media with the same id already exists");
    }

    if (sampleRate == 0) {
        throw SamplerException("Unable to initialize media with zero sampleRate");
    }

    if (channels <= 0) {
        throw SamplerException("Unable to initialize media with zero channels");
    }

    auto media = new Media(sampleRate, channels);
    mediaPool.emplace(id, media);
}

void Sampler::start(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->start();
}

void Sampler::play(int64_t id, uint8_t *samples, uint64_t size) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->play(samples, size);
}

void Sampler::stop(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->stop();
}

void Sampler::close(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _releaseMedia(id);
}