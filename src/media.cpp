#include "media.h"

#define CHECK_AL_ERROR() _checkALError(__FILE__, __LINE__)

void Media::_checkALError(const char *file, int line) {
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "OpenAL error at " << file << ":" << line << " - " << alGetString(error) << std::endl;
    }
}

void Media::_discardQueuedBuffers() const {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &buffers);
    CHECK_AL_ERROR();
    if (buffers > 0) {
        std::vector<ALuint> out(buffers);
        alSourceUnqueueBuffers(source, buffers, out.data());
        CHECK_AL_ERROR();
        alDeleteBuffers(buffers, out.data());
        CHECK_AL_ERROR();
    }
}

void Media::_discardProcessedBuffers() const {
    ALint buffers = 0;
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffers);
    CHECK_AL_ERROR();
    if (buffers > 0) {
        std::vector<ALuint> out(buffers);
        alSourceUnqueueBuffers(source, buffers, out.data());
        CHECK_AL_ERROR();
        alDeleteBuffers(buffers, out.data());
        CHECK_AL_ERROR();
    }
}

Media::Media(uint32_t sampleRate, uint32_t channels, uint32_t numBuffers) {
    std::lock_guard<std::mutex> lock(mutex);

    this->sampleRate = sampleRate;

    this->channels = channels;

    this->numBuffers = numBuffers;

    this->format = (channels == 1) ? AL_FORMAT_MONO_FLOAT32 : (channels == 2) ? AL_FORMAT_STEREO_FLOAT32 : AL_NONE;

    if (this->format == AL_NONE) {
        std::cerr << "Unsupported audio format." << std::endl;
        return;
    }

    this->stretch = new signalsmith::stretch::SignalsmithStretch<float>();

    this->stretch->presetDefault((int) channels, (float) sampleRate);

    alGenSources(1, &this->source);
    CHECK_AL_ERROR();

    if (alGetError() != AL_NO_ERROR) {
        this->source = AL_NONE;
        return;
    }
}

Media::~Media() {
    std::lock_guard<std::mutex> lock(mutex);

    if (this->source != AL_NONE) {
        alSourceStop(this->source);
        CHECK_AL_ERROR();

        _discardQueuedBuffers();
        _discardProcessedBuffers();

        alDeleteSources(1, &this->source);
        CHECK_AL_ERROR();

        this->source = AL_NONE;
    }

    if (stretch != nullptr) {
        stretch->reset();
        delete stretch;
        stretch = nullptr;
    }
}

float Media::getCurrentTime() {
    std::lock_guard<std::mutex> lock(mutex);

    if (source == AL_NONE) {
        return 0.0f;
    }

    ALfloat currentTime;
    alGetSourcef(source, AL_SEC_OFFSET, &currentTime);
    CHECK_AL_ERROR();

    return currentTime;
}

void Media::setPlaybackSpeed(float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    playbackSpeedFactor = factor;
}

bool Media::setVolume(float value) {
    std::lock_guard<std::mutex> lock(mutex);

    if (source == AL_NONE) {
        return false;
    }

    if (value < 0.0f || value > 1.0f) {
        std::cerr << "Volume value out of range: " << value << std::endl;
        return false;
    }

    ALfloat currentVolume;
    alGetSourcef(source, AL_GAIN, &currentVolume);
    CHECK_AL_ERROR();

    return std::fabs(currentVolume - value) <= 0.01;
}

bool Media::play(const uint8_t *samples, uint64_t size) {
    std::unique_lock<std::mutex> lock(mutex);

    if (!stretch || source == AL_NONE) {
        std::cerr << "Error: Stretch or source is not initialized." << std::endl;
        return false;
    }

    if (size <= 0) {
        std::cerr << "Unable to play empty samples." << std::endl;
        return false;
    }

    ALint sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();
    if (sourceState == AL_INITIAL) {
        alSourcePlay(source);
        CHECK_AL_ERROR();
    }

    if (sourceState == AL_PLAYING) {
        ALint buffersQueued;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &buffersQueued);
        CHECK_AL_ERROR();

        if (buffersQueued >= numBuffers) {
            return false;
        }

        ALint buffersProcessed;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffersProcessed);
        CHECK_AL_ERROR();

        ALuint buffer;
        if (buffersProcessed > 0) {
            alSourceUnqueueBuffers(source, 1, &buffer);
            CHECK_AL_ERROR();
        } else {
            alGenBuffers(1, &buffer);
            CHECK_AL_ERROR();
        }

        int inputSamples = static_cast<int>((float) size / sizeof(float) / (float) channels);
        int outputSamples = static_cast<int>((float) inputSamples / playbackSpeedFactor);

        std::vector<std::vector<float>> inputBuffers(channels, std::vector<float>(inputSamples));
        std::vector<std::vector<float>> outputBuffers(channels, std::vector<float>(outputSamples));

        for (int i = 0; i < inputSamples * channels; ++i) {
            inputBuffers[i % channels][i / channels] = reinterpret_cast<const float *>(samples)[i];
        }

        stretch->process(inputBuffers, inputSamples, outputBuffers, outputSamples);

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

        return true;
    }

    return false;
}

void Media::pause() {
    std::lock_guard<std::mutex> lock(mutex);

    if (source == AL_NONE) {
        return;
    }

    ALenum sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();
    if (sourceState == AL_PLAYING) {
        alSourcePause(source);
        CHECK_AL_ERROR();
    }
}

void Media::resume() {
    std::lock_guard<std::mutex> lock(mutex);

    if (source == AL_NONE) {
        return;
    }

    ALint sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();
    if (sourceState == AL_PAUSED) {
        alSourcePlay(source);
        CHECK_AL_ERROR();
    }
}

void Media::stop() {
    std::lock_guard<std::mutex> lock(mutex);

    if (source == AL_NONE) {
        return;
    }

    ALint sourceState;
    alGetSourcei(source, AL_SOURCE_STATE, &sourceState);
    CHECK_AL_ERROR();
    if (sourceState == AL_PLAYING || sourceState == AL_PAUSED) {
        alSourceStop(source);
        CHECK_AL_ERROR();

        _discardQueuedBuffers();
        _discardProcessedBuffers();

        alSourcei(source, AL_BUFFER, 0);
        CHECK_AL_ERROR();

        alSourceRewind(source);
        CHECK_AL_ERROR();
    }
}