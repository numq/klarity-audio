#include "sampler.h"

#define CHECK_AL_ERROR() _checkALError(__FILE__, __LINE__)

std::vector<uint8_t> Sampler::_convertSamples(float *samples, uint64_t size) {
    size_t numS16Samples = size / 2;

    std::vector<uint8_t> s16Samples(numS16Samples * sizeof(int16_t) * 2);

    for (size_t i = 0; i < numS16Samples; ++i) {
        float floatValue1 = std::max(std::min(samples[i * 2], 1.0f), -1.0f);
        float floatValue2 = std::max(std::min(samples[i * 2 + 1], 1.0f), -1.0f);

        auto s16Value1 = static_cast<int16_t>(floatValue1 * 32767.0f);
        auto s16Value2 = static_cast<int16_t>(floatValue2 * 32767.0f);

        s16Samples[i * sizeof(int16_t) * 2] = static_cast<uint8_t>(s16Value1 & 0xFF);
        s16Samples[i * sizeof(int16_t) * 2 + 1] = static_cast<uint8_t>((s16Value1 >> 8) & 0xFF);
        s16Samples[i * sizeof(int16_t) * 2 + 2] = static_cast<uint8_t>(s16Value2 & 0xFF);
        s16Samples[i * sizeof(int16_t) * 2 + 3] = static_cast<uint8_t>((s16Value2 >> 8) & 0xFF);
    }

    return s16Samples;
}

void Sampler::_checkALError(const char *file, int line) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << " - " << alGetString(error) << std::endl;
    }
}

ALenum Sampler::_getFormat(uint32_t bitsPerSample, uint32_t channels) {
    if (channels == 1) {
        if (bitsPerSample == 8) {
            return AL_FORMAT_MONO8;
        } else if (bitsPerSample == 16) {
            return AL_FORMAT_MONO16;
        }
    } else if (channels == 2) {
        if (bitsPerSample == 8) {
            return AL_FORMAT_STEREO8;
        } else if (bitsPerSample == 16) {
            return AL_FORMAT_STEREO16;
        }
    }
    return AL_NONE;
}

void Sampler::_discardQueuedBuffers() const {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &buffers);
    if (buffers > 0) {
        ALuint out[buffers];
        alSourceUnqueueBuffers(source, buffers, out);
        alDeleteBuffers(buffers, out);
    }
}

void Sampler::_discardProcessedBuffers() const {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffers);
    if (buffers > 0) {
        ALuint out[buffers];
        alSourceUnqueueBuffers(source, buffers, out);
        alDeleteBuffers(buffers, out);
    }
}

void Sampler::_cleanUp() {
    alDeleteSources(1, &source);

    alcMakeContextCurrent(nullptr);

    alcDestroyContext(context);

    alcCloseDevice(device);

    delete stretch;
}

Sampler::Sampler(uint32_t bitsPerSample, uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    this->sampleRate = sampleRate;

    device = alcOpenDevice(nullptr);
    if (!device) {
        std::cerr << "Failed to open OpenAL device." << std::endl;
        return;
    }

    context = alcCreateContext(device, nullptr);
    if (!context || alcMakeContextCurrent(context) == ALC_FALSE) {
        std::cerr << "Failed to create or set OpenAL context." << std::endl;
        alcCloseDevice(device);
        return;
    }

    format = _getFormat(bitsPerSample, channels);
    if (format == AL_NONE) {
        std::cerr << "Unable to convert audio format." << std::endl;
        return;
    }

    alGenSources(1, &source);
    CHECK_AL_ERROR();

    stretch = new Stretch();

    stretch->presetDefault((int) channels, (float) sampleRate);
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    _discardQueuedBuffers();

    _discardProcessedBuffers();

    _cleanUp();
}

void Sampler::setPlaybackSpeed(float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    this->playbackSpeedFactor = factor;
}

bool Sampler::setVolume(float value) {
    std::lock_guard<std::mutex> lock(mutex);

    if (0.0f <= value && value <= 1.0f) {
        alSourcef(source, AL_GAIN, value);
        CHECK_AL_ERROR();

        ALfloat currentVolume;
        alGetSourcef(source, AL_GAIN, &currentVolume);
        CHECK_AL_ERROR();

        return std::fabs(currentVolume - value) <= 0.01;
    }

    return false;
}

bool Sampler::play(uint8_t *samples, uint64_t size) {
    std::lock_guard<std::mutex> lock(mutex);

    _discardProcessedBuffers();

    ALint buffersQueued = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &buffersQueued);
    if (buffersQueued < MIN_BUFFERS_NUM) {
        std::vector<uint8_t> pcmSamples;

        auto stretchedSamples = stretch->process(
                reinterpret_cast<float *>(samples),
                int(size / sizeof(float)),
                playbackSpeedFactor
        );

        pcmSamples = _convertSamples(stretchedSamples.data(), stretchedSamples.size());

        if (!pcmSamples.empty()) {
            ALuint buffer;
            alGenBuffers(1, &buffer);
            CHECK_AL_ERROR();

            alBufferData(
                    buffer,
                    format,
                    (ALvoid *) pcmSamples.data(),
                    (ALsizei) pcmSamples.size(),
                    (ALsizei) sampleRate
            );
            CHECK_AL_ERROR();

            alSourceQueueBuffers(source, 1, &buffer);
            CHECK_AL_ERROR();
        }

        ALint sourceState;
        alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
        if (sourceState != AL_PLAYING) {
            alSourcePlay(source);
        }

        return true;
    }

    return false;
}

void Sampler::pause() {
    std::lock_guard<std::mutex> lock(mutex);

    alSourcePause(source);
    CHECK_AL_ERROR();
}

void Sampler::resume() {
    std::lock_guard<std::mutex> lock(mutex);

    ALint sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);

    if (sourceState == AL_PAUSED) {
        alSourcePlay(source);
        CHECK_AL_ERROR();
    }
}

void Sampler::stop() {
    std::lock_guard<std::mutex> lock(mutex);

    alSourceStop(source);
    CHECK_AL_ERROR();

    _discardQueuedBuffers();

    _discardProcessedBuffers();

    stretch->reset();
}