#include "sampler.h"

#define CHECK_AL_ERROR() _checkALError(__FILE__, __LINE__)

void Sampler::_checkALError(const char *file, int line) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << " - " << alGetString(error) << std::endl;
    }
}

Media *Sampler::_acquireMedia(uint64_t id) {
    auto it = mediaPool.find(id);
    if (it == mediaPool.end()) {
        return nullptr;
    }
    return it->second;
}

void Sampler::_releaseMedia(uint64_t id) {
    auto it = mediaPool.find(id);
    if (it != mediaPool.end()) {
        delete it->second;
        mediaPool.erase(it);
    }
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &media: mediaPool) {
        _releaseMedia(media.first);
    }
}

float Sampler::getCurrentTime(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        return media->getCurrentTime();
    }

    return 0.0;
}

void Sampler::setPlaybackSpeed(uint64_t id, float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        media->setPlaybackSpeed(factor);
    }
}

bool Sampler::setVolume(uint64_t id, float value) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        return media->setVolume(value);
    }

    return false;
}

bool Sampler::initialize(uint64_t id, uint32_t sampleRate, uint32_t channels, uint32_t numBuffers) {
    std::lock_guard<std::mutex> lock(mutex);

    if (mediaPool.find(id) == mediaPool.end()) {
        auto media = new Media(sampleRate, channels, numBuffers);

        mediaPool.emplace(id, media);

        return true;
    }

    return false;
}

bool Sampler::play(uint64_t id, uint8_t *samples, uint64_t size) {
    std::unique_lock<std::mutex> lock(mutex);

    if (size <= 0) {
        std::cerr << "Unable to play empty samples." << std::endl;
        return false;
    }

    auto media = _acquireMedia(id);

    if (media) {
        return media->play(samples, size);
    }

    return false;
}

void Sampler::pause(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        media->pause();
    }
}

void Sampler::resume(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        media->resume();
    }
}

void Sampler::stop(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (media) {
        media->stop();
    }
}

void Sampler::close(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _releaseMedia(id);
}