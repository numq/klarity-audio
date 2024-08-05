#include "media.h"

Media::Media(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    this->sampleRate = sampleRate;

    this->channels = channels;

    this->format = paFloat32;

    this->stream = nullptr;

    stretch = new signalsmith::stretch::SignalsmithStretch<float>();

    stretch->presetDefault((int) channels, (float) sampleRate);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return;
    }
}

Media::~Media() {
    std::lock_guard<std::mutex> lock(mutex);

    if (stream != nullptr) {
        Pa_AbortStream(stream);
        Pa_CloseStream(stream);
        stream = nullptr;
    }

    if (stretch != nullptr) {
        stretch->reset();
        delete stretch;
        stretch = nullptr;
    }
}

int64_t Media::getCurrentTimeMicros() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return -1;
    }

    long framesAvailable = Pa_GetStreamReadAvailable(stream);

    double streamTime = Pa_GetStreamTime(stream);
    if (streamTime < 0) {
        std::cerr << "Error getting stream time." << std::endl;
        return -1;
    }

    return static_cast<int64_t>(streamTime - static_cast<float>(framesAvailable) / static_cast<float>(sampleRate));
}

void Media::setPlaybackSpeed(float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return;
    }

    playbackSpeedFactor = factor;
}

void Media::setVolume(float value) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return;
    }

    volume = value;
}

bool Media::start() {
    std::unique_lock<std::mutex> lock(mutex);

    if (!stretch) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return false;
    }

    if (stream) {
        std::cerr << "Unable to start active sampler." << std::endl;
        return false;
    }

    PaDeviceIndex deviceIndex = Pa_GetDefaultOutputDevice();

    if (deviceIndex == paNoDevice) {
        std::cerr << "Error: No default output device." << std::endl;
        return false;
    }

    PaStreamParameters outputParameters;
    outputParameters.device = deviceIndex;
    outputParameters.channelCount = static_cast<int>(this->channels);
    outputParameters.sampleFormat = this->format;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(
            &stream,
            nullptr,
            &outputParameters,
            sampleRate,
            paFramesPerBufferUnspecified,
            paNoFlag,
            nullptr,
            nullptr
    );
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "Failed to start PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    return true;
}

bool Media::play(const uint8_t *samples, uint64_t size) {
    std::unique_lock<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr || Pa_IsStreamActive(stream) <= 0) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return false;
    }

    if (size <= 0) {
        std::cerr << "Unable to play empty samples." << std::endl;
        return false;
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
            output.push_back(outputBuffers[ch][i] * volume);
        }
    }

    if (Pa_IsStreamActive(stream) == 0) {
        PaError err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "Failed to resume PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
    }

    PaError err = Pa_WriteStream(stream, output.data(), outputSamples);
    if (err != paNoError) {
        std::cerr << "Failed to write PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        return false;
    }

    return true;
}

bool Media::pause() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return false;
    }

    if (Pa_IsStreamActive(stream) == 1) {
        PaError err = Pa_StopStream(stream);
        if (err != paNoError) {
            std::cerr << "Failed to pause PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        return true;
    }

    return false;
}

bool Media::resume() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return false;
    }

    if (Pa_IsStreamStopped(stream) == 1) {
        PaError err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "Failed to resume PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }
        return true;
    }

    return false;
}

bool Media::stop() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        std::cerr << "Unable to use uninitialized sampler." << std::endl;
        return false;
    }

    if (Pa_IsStreamActive(stream) == 1) {
        PaError err = Pa_AbortStream(stream);
        if (err != paNoError) {
            std::cerr << "Failed to stop PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        err = Pa_CloseStream(stream);
        if (err != paNoError) {
            std::cerr << "Failed to close PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
            return false;
        }

        stream = nullptr;

        return true;
    }

    return false;
}