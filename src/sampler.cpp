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
        std::cerr << "Unable to find media" << std::endl;
    }
    return it->second;
}

void Sampler::_releaseMedia(uint64_t id) {
    auto it = mediaPool.find(id);
    if (it != mediaPool.end()) {
        alSourceStop(it->second->source);
        CHECK_AL_ERROR();

        _discardQueuedBuffers(it->second->source);

        _discardProcessedBuffers(it->second->source);

        it->second->stretch->reset();

        delete it->second;

        mediaPool.erase(it);
    }
}

void Sampler::_discardQueuedBuffers(ALuint source) {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &buffers);
    if (buffers > 0) {
        std::vector<ALuint> out(buffers);
        alSourceUnqueueBuffers(source, buffers, out.data());
        alDeleteBuffers(buffers, out.data());
    }
}

void Sampler::_discardProcessedBuffers(ALuint source) {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffers);
    if (buffers > 0) {
        std::vector<ALuint> out(buffers);
        alSourceUnqueueBuffers(source, buffers, out.data());
        alDeleteBuffers(buffers, out.data());
    }
}

Sampler::Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    auto deviceName = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
    this->device = alcOpenDevice(deviceName);
    if (!this->device) {
        std::cerr << "Failed to open OpenAL device." << std::endl;
    }

    this->context = alcCreateContext(this->device, nullptr);
    if (!this->context) {
        std::cerr << "Failed to create OpenAL context." << std::endl;
        alcCloseDevice(this->device);
    }

    if (alcMakeContextCurrent(this->context) == ALC_FALSE) {
        std::cerr << "Failed to make OpenAL context current." << std::endl;
        alcDestroyContext(this->context);
        alcCloseDevice(this->device);
    }
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &media: mediaPool) {
        _discardQueuedBuffers(media.second->source);
        _discardProcessedBuffers(media.second->source);
        _releaseMedia(media.first);
    }

    alcMakeContextCurrent(nullptr);
    if (this->context) {
        alcDestroyContext(this->context);
        this->context = nullptr;
    }

    if (this->device) {
        alcCloseDevice(this->device);
        this->device = nullptr;
    }

    mediaPool.clear();
}

float Sampler::getCurrentTime(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);

    ALfloat currentTime;
    alGetSourcef(media->source, AL_SEC_OFFSET, &currentTime);
    CHECK_AL_ERROR();

    return currentTime;
}

void Sampler::setPlaybackSpeed(uint64_t id, float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    _acquireMedia(id)->changePlaybackSpeed(factor);
}

bool Sampler::setVolume(uint64_t id, float value) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);

    if (0.0f <= value && value <= 1.0f) {
        alSourcef(media->source, AL_GAIN, value);
        CHECK_AL_ERROR();

        ALfloat currentVolume;
        alGetSourcef(media->source, AL_GAIN, &currentVolume);
        CHECK_AL_ERROR();

        return std::fabs(currentVolume - value) <= 0.01;
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

    auto media = _acquireMedia(id);

    ALint buffersQueued;
    alGetSourcei(media->source, AL_BUFFERS_QUEUED, &buffersQueued);
    CHECK_AL_ERROR();

    ALint buffersProcessed;
    alGetSourcei(media->source, AL_BUFFERS_PROCESSED, &buffersProcessed);
    CHECK_AL_ERROR();

    ALuint buffer;
    if (buffersProcessed > 0) {
        alSourceUnqueueBuffers(media->source, 1, &buffer);
        CHECK_AL_ERROR();
        return false;
    } else if (buffersQueued >= media->numBuffers) {
        return false;
    }

    int inputSamples = static_cast<int>((float) size / sizeof(float) / (float) media->channels);
    int outputSamples = static_cast<int>((float) inputSamples / media->playbackSpeedFactor);

    std::vector<std::vector<float>> inputBuffers(media->channels, std::vector<float>(inputSamples));
    std::vector<std::vector<float>> outputBuffers(media->channels, std::vector<float>(outputSamples));

    for (int i = 0; i < inputSamples * media->channels; ++i) {
        inputBuffers[i % media->channels][i / media->channels] = reinterpret_cast<float *>(samples)[i];
    }

    media->stretch->process(
            inputBuffers,
            inputSamples,
            outputBuffers,
            outputSamples
    );

    std::vector<float> output;
    output.reserve(outputSamples * media->channels);
    for (int i = 0; i < outputSamples; ++i) {
        for (int ch = 0; ch < media->channels; ++ch) {
            output.push_back(outputBuffers[ch][i]);
        }
    }

    alGenBuffers(1, &buffer);
    CHECK_AL_ERROR();

    alBufferData(
            buffer,
            media->format,
            (ALvoid *) output.data(),
            (ALsizei) (output.size() * sizeof(float)),
            (ALsizei) media->sampleRate
    );
    CHECK_AL_ERROR();

    alSourceQueueBuffers(media->source, 1, &buffer);
    CHECK_AL_ERROR();

    ALint sourceState;
    alGetSourcei(media->source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();

    if (sourceState != AL_PLAYING) {
        alSourcePlay(media->source);
        CHECK_AL_ERROR();
    }

    return true;
}

void Sampler::pause(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);

    ALenum playbackState;
    alGetSourcei(media->source, AL_SOURCE_STATE, &playbackState);
    if (playbackState == AL_PLAYING) {
        alSourcePause(media->source);
        CHECK_AL_ERROR();
    }
}

void Sampler::resume(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);

    ALint sourceState;
    alGetSourcei(media->source, AL_SOURCE_STATE, &sourceState);
    if (sourceState == AL_PAUSED) {
        alSourcePlay(media->source);
        CHECK_AL_ERROR();
    }
}

void Sampler::stop(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    auto media = _acquireMedia(id);

    _discardQueuedBuffers(media->source);

    _discardProcessedBuffers(media->source);

    alSourceStop(media->source);
    CHECK_AL_ERROR();
}

void Sampler::close(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex);

    _releaseMedia(id);
}