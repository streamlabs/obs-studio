#pragma once

#include "media-io/audio-io.h"

#define OBS_AUDIO_ROUTE_WORDS ((AUDIO_OUTPUT_FRAMES + 63) / 64)

struct obs_audio_route_gate {
	uint64_t bits[OBS_AUDIO_ROUTE_WORDS];
};

static inline uint64_t obs_audio_route_gate_word(const struct obs_audio_route_gate *gate, int64_t first_bit)
{
	if (first_bit <= -64 || first_bit >= AUDIO_OUTPUT_FRAMES)
		return 0;
	if (first_bit < 0)
		return gate->bits[0] << -first_bit;
	size_t word = (size_t)first_bit / 64;
	size_t shift = (size_t)first_bit % 64;
	uint64_t result = gate->bits[word] >> shift;
	if (shift && word + 1 < OBS_AUDIO_ROUTE_WORDS)
		result |= gate->bits[word + 1] << (64 - shift);
	return result;
}

static inline int64_t obs_audio_route_frame_offset(uint64_t timestamp, uint64_t origin, size_t sample_rate)
{
	uint64_t distance = timestamp >= origin ? timestamp - origin : origin - timestamp;
	uint64_t frames = ns_to_audio_frames(sample_rate, distance);
	if (frames > AUDIO_OUTPUT_FRAMES)
		frames = AUDIO_OUTPUT_FRAMES;
	return timestamp >= origin ? (int64_t)frames : -(int64_t)frames;
}

static inline bool obs_audio_route_gate_merge(struct obs_audio_route_gate *out,
					      const struct obs_audio_route_gate *parent,
					      const struct obs_audio_route_gate *edge, uint64_t child_ts,
					      uint64_t parent_ts, uint64_t edge_ts, size_t sample_rate)
{
	int64_t parent_offset = obs_audio_route_frame_offset(child_ts, parent_ts, sample_rate);
	int64_t edge_offset = obs_audio_route_frame_offset(child_ts, edge_ts, sample_rate);
	bool changed = false;
	for (size_t w = 0; w < OBS_AUDIO_ROUTE_WORDS; w++) {
		uint64_t bits = obs_audio_route_gate_word(parent, (int64_t)(w * 64) + parent_offset) &
				obs_audio_route_gate_word(edge, (int64_t)(w * 64) + edge_offset);
		changed |= (bits & ~out->bits[w]) != 0;
		out->bits[w] |= bits;
	}
	return changed;
}
