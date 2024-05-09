#include "sampler.h"

#define CHECK_AL_ERROR() _checkALError(__FILE__, __LINE__)

void Sampler::_checkALError(const char *file, int line) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << " - " << alGetString(error) << std::endl;
    }
}

ALenum Sampler::_getFormat(uint32_t channels) {
    if (channels == 1) {
        return AL_FORMAT_MONO_FLOAT32;
    } else if (channels == 2) {
        return AL_FORMAT_STEREO_FLOAT32;
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

void Sampler::_initialize(uint32_t channels) {
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

    format = _getFormat(channels);
    if (format == AL_NONE) {
        std::cerr << "Unable to convert audio format." << std::endl;
        return;
    }

    stretch = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
    stretch->presetDefault((int) channels, (float) sampleRate);

    alGenSources(1, &source);
    CHECK_AL_ERROR();
}

void Sampler::_cleanUp() {
    alDeleteSources(1, &source);

    alcMakeContextCurrent(nullptr);

    alcDestroyContext(context);

    alcCloseDevice(device);

    stretch->reset();
}

Sampler::Sampler(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    this->sampleRate = sampleRate;

    this->channels = channels;

    _initialize(channels);
}

Sampler::Sampler(uint32_t sampleRate, uint32_t channels, uint32_t numBuffers) {
    std::lock_guard<std::mutex> lock(mutex);

    this->sampleRate = sampleRate;

    this->channels = channels;

    this->numBuffers = numBuffers;

    _initialize(channels);
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
    std::unique_lock<std::mutex> lock(mutex);

    ALint buffersQueued;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &buffersQueued);
    CHECK_AL_ERROR();

    ALint buffersProcessed;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffersProcessed);
    CHECK_AL_ERROR();

    ALuint buffer;
    if (buffersProcessed > 0) {
        alSourceUnqueueBuffers(source, 1, &buffer);
        CHECK_AL_ERROR();
    } else if (buffersQueued < numBuffers) {
        alGenBuffers(1, &buffer);
        CHECK_AL_ERROR();
    } else {
        return false;
    }

    int inputSamples = static_cast<int>((float) size / sizeof(float) / (float) channels);
    int outputSamples = static_cast<int>((float) inputSamples / playbackSpeedFactor);

    std::vector<std::vector<float>> inputBuffers(channels, std::vector<float>(inputSamples));
    std::vector<std::vector<float>> outputBuffers(channels, std::vector<float>(outputSamples));

    for (int i = 0; i < inputSamples * channels; ++i) {
        inputBuffers[i % channels][i / channels] = reinterpret_cast<float *>(samples)[i];
    }

    stretch->process(
            inputBuffers,
            inputSamples,
            outputBuffers,
            outputSamples
    );

    std::vector<float> output;
    output.reserve(outputSamples * channels);
    for (int i = 0; i < outputSamples; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            output.push_back(outputBuffers[ch][i]);
        }
    }

    alBufferData(
            buffer,
            format,
            (ALvoid *) output.data(),
            (ALsizei) (output.size() * sizeof(float)),
            (ALsizei) sampleRate
    );
    CHECK_AL_ERROR();

    alSourceQueueBuffers(source, 1, &buffer);
    CHECK_AL_ERROR();

    ALint sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();

    if (sourceState != AL_PLAYING) {
        alSourcePlay(source);
        CHECK_AL_ERROR();
    }

    return true;
}

void Sampler::pause() {
    std::lock_guard<std::mutex> lock(mutex);

    ALenum playbackState;
    alGetSourcei(source, AL_SOURCE_STATE, &playbackState);
    if (playbackState == AL_PLAYING) {
        alSourcePause(source);
        CHECK_AL_ERROR();
    }
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