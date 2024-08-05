#include "sampler.h"

Media *Sampler::_acquireMedia(int64_t id) {
    auto it = mediaPool.find(id);
    if (it == mediaPool.end()) {
        return nullptr;
    }
    return it->second;
}

void Sampler::_releaseMedia(int64_t id) {
    auto it = mediaPool.find(id);
    if (it == mediaPool.end()) {
        return;
    }

    delete it->second;
    mediaPool.erase(it);
}

Sampler::Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
    }
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &media: mediaPool) {
        _releaseMedia(media.first);
    }

    Pa_Terminate();
}

int64_t Sampler::getCurrentTimeMicros(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return -1.0;
    }

    return media->getCurrentTimeMicros();
}

void Sampler::setPlaybackSpeed(int64_t id, float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return;
    }

    media->setPlaybackSpeed(factor);
}

void Sampler::setVolume(int64_t id, float value) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return;
    }

    return media->setVolume(value);
}

bool Sampler::initialize(int64_t id, uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    if (mediaPool.find(id) != mediaPool.end()) {
        std::cerr << "Media with the same id already exists." << std::endl;
        return false;
    }

    if (sampleRate == 0) {
        std::cerr << "Unable to initialize media with zero sampleRate." << std::endl;
        return false;
    }

    if (channels <= 0) {
        std::cerr << "Unable to initialize media with zero channels." << std::endl;
        return false;
    }

    auto media = new Media(sampleRate, channels);
    mediaPool.emplace(id, media);
    return true;
}

bool Sampler::start(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return false;
    }

    return media->start();
}

bool Sampler::play(int64_t id, uint8_t *samples, uint64_t size) {
    std::lock_guard<std::mutex> lock(mutex);

    if (size <= 0) {
        std::cerr << "Unable to play empty samples." << std::endl;
        return false;
    }

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return false;
    }

    return media->play(samples, size);
}

bool Sampler::pause(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return false;
    }

    return media->pause();
}

bool Sampler::resume(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return false;
    }

    return media->resume();
}

bool Sampler::stop(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);
    if (!media) {
        std::cerr << "Unable to find media." << std::endl;
        return false;
    }

    return media->stop();
}

void Sampler::close(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _releaseMedia(id);
}