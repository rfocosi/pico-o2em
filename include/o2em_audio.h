#ifndef O2EM_AUDIO_H
#define O2EM_AUDIO_H

#include <stdint.h>
#include <stddef.h>

// Initialize audio subsystem (PWM, GPIO pins)
void audio_init(void);

// Queue stereo/mono samples to the audio circular buffer
// samples: array of interleaved 16-bit signed samples
// num_frames: number of audio frames (stereo pair / mono sample)
void audio_play_samples(const int16_t *samples, size_t num_frames);

#endif // O2EM_AUDIO_H
