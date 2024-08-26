#include "sampler.h"

Sampler::Sampler(uint32_t sampleRate, uint32_t channels) {
    std::lock_guard<std::mutex> lock(mutex);

    this->channels = channels;

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw SamplerException("Failed to initialize PortAudio: " + std::string(Pa_GetErrorText(err)));
    }

    PaDeviceIndex deviceIndex = Pa_GetDefaultOutputDevice();
    if (deviceIndex == paNoDevice) {
        throw SamplerException("Error: No default output device");
    }

    PaStreamParameters outputParameters;
    outputParameters.device = deviceIndex;
    outputParameters.channelCount = static_cast<int>(channels);
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    PaStream *rawStream = nullptr;
    err = Pa_OpenStream(
            &rawStream,
            nullptr,
            &outputParameters,
            sampleRate,
            paFramesPerBufferUnspecified,
            paNoFlag,
            nullptr,
            nullptr
    );
    if (err != paNoError) {
        throw SamplerException("PortAudio error: " + std::string(Pa_GetErrorText(err)));
    }

    stream.reset(rawStream);

    stretch.reset(new signalsmith::stretch::SignalsmithStretch<float>());

    stretch->presetDefault((int) channels, (float) sampleRate);
}

Sampler::~Sampler() {
    std::lock_guard<std::mutex> lock(mutex);

    Pa_Terminate();
}

void Sampler::setPlaybackSpeed(float factor) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        throw SamplerException("Unable to use uninitialized sampler");
    }

    playbackSpeedFactor = factor;
}

void Sampler::setVolume(float value) {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        throw SamplerException("Unable to use uninitialized sampler");
    }

    volume = value;
}

void Sampler::start() {
    std::unique_lock<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        throw SamplerException("Unable to use uninitialized sampler");
    }

    if (Pa_IsStreamActive(stream.get()) == 1) {
        throw SamplerException("Unable to start active sampler");
    }

    PaError err = Pa_StartStream(stream.get());
    if (err != paNoError) {
        throw SamplerException("Failed to start PortAudio stream: " + std::string(Pa_GetErrorText(err)));
    }
}

void Sampler::play(const uint8_t *samples, uint64_t size) {
    std::unique_lock<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr || Pa_IsStreamActive(stream.get()) <= 0) {
        throw SamplerException("Unable to use uninitialized sampler");
    }

    if (size <= 0) {
        throw SamplerException("Unable to play empty samples");
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

    PaError err = Pa_WriteStream(stream.get(), output.data(), outputSamples);
    if (err != paNoError) {
        throw SamplerException("Failed to write PortAudio stream: " + std::string(Pa_GetErrorText(err)));
    }
}

void Sampler::stop() {
    std::lock_guard<std::mutex> lock(mutex);

    if (!stretch || stream == nullptr) {
        throw SamplerException("Unable to use uninitialized sampler");
    }

    if (Pa_IsStreamActive(stream.get()) == 1) {
        PaError err = Pa_StopStream(stream.get());
        if (err != paNoError) {
            throw SamplerException("Failed to stop PortAudio stream: " + std::string(Pa_GetErrorText(err)));
        }

        stretch->reset();
    }
}