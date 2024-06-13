#ifndef KLARITY_SAMPLER_MEDIA_H
#define KLARITY_SAMPLER_MEDIA_H

#include <iostream>
#include "stretch/stretch.h"
#include "al.h"
#include "alc.h"
#include "alext.h"

struct Media {
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t numBuffers;
    ALenum format = AL_NONE;
    ALuint source = AL_NONE;
    signalsmith::stretch::SignalsmithStretch<float> *stretch;
    float playbackSpeedFactor = 1.0f;

public:
    explicit Media(uint32_t sampleRate, uint32_t channels, uint32_t numBuffers);

    ~Media();

    static void _checkALError(const char *file, int line);

    void changePlaybackSpeed(float factor);
};

#endif //KLARITY_SAMPLER_MEDIA_H
