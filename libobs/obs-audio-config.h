#pragma once

#include "media-io/audio-resampler.h"
#include "util/threading.h"

#include <string.h>

/* A producer uses one value snapshot for the entire submitted audio packet. */
struct audio_output_config {
	struct resample_info info;
	size_t planes;
	size_t blocksize;
	size_t channels;
};

/* This state must outlive audio output resets and all source audio callbacks.
 * Its mutex protects only the copied configuration, never the output object or
 * rendering, allocation, callbacks, and audio thread startup/shutdown. */
struct audio_output_config_state {
	pthread_mutex_t mutex;
	bool initialized;
	bool available;
	struct audio_output_config config;
};

static inline bool audio_output_config_init(struct audio_output_config_state *state)
{
	memset(state, 0, sizeof(*state));
	pthread_mutex_init_value(&state->mutex);
	state->initialized = pthread_mutex_init(&state->mutex, NULL) == 0;
	return state->initialized;
}

/* Only call after all producers and configuration writers have stopped. */
static inline void audio_output_config_destroy(struct audio_output_config_state *state)
{
	if (!state->initialized)
		return;

	pthread_mutex_destroy(&state->mutex);
	pthread_mutex_init_value(&state->mutex);
	state->initialized = false;
	state->available = false;
	memset(&state->config, 0, sizeof(state->config));
}

/* A NULL configuration unpublishes the output before it is closed. */
static inline void audio_output_config_set(struct audio_output_config_state *state,
					   const struct audio_output_config *config)
{
	if (!state->initialized)
		return;

	pthread_mutex_lock(&state->mutex);
	if (config)
		state->config = *config;
	else
		memset(&state->config, 0, sizeof(state->config));
	state->available = config != NULL;
	pthread_mutex_unlock(&state->mutex);
}

static inline bool audio_output_config_get(struct audio_output_config_state *state, struct audio_output_config *config)
{
	if (!state->initialized) {
		memset(config, 0, sizeof(*config));
		return false;
	}

	pthread_mutex_lock(&state->mutex);
	*config = state->config;
	bool available = state->available;
	pthread_mutex_unlock(&state->mutex);
	return available;
}
