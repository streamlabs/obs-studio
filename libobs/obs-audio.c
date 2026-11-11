/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <inttypes.h>
#include <math.h>
#include "obs-internal.h"
#include "util/util_uint64.h"

struct ts_info {
	uint64_t start;
	uint64_t end;
};

#define DEBUG_AUDIO 0
#define DEBUG_LAGGED_AUDIO 0

// Cached state of multiple rendering so each run of in audio-io thread work with same state
static bool audio_multiple_rendering = false;

void cache_multiple_rendering(void)
{
	audio_multiple_rendering = obs_get_multiple_rendering();
}

bool get_cached_multiple_rendering(void)
{
	return audio_multiple_rendering;
}

static THREAD_LOCAL struct obs_core_audio *route_audio;

static size_t find_route_node(const struct obs_core_audio *audio, const obs_source_t *source)
{
	for (size_t i = 0; i < audio->route_nodes.num; i++) {
		if (audio->route_nodes.array[i].source == source)
			return i;
	}
	return DARRAY_INVALID;
}

static size_t route_node(struct obs_core_audio *audio, obs_source_t *source)
{
	size_t idx = find_route_node(audio, source);
	if (idx != DARRAY_INVALID)
		return idx;

	struct obs_audio_route_node node = {.source = obs_source_get_ref(source), .first_edge = DARRAY_INVALID};
	if (!node.source)
		return DARRAY_INVALID;
	da_push_back(audio->route_nodes, &node);
	return audio->route_nodes.num - 1;
}

static struct obs_audio_route_edge *add_audio_route(obs_source_t *parent, obs_source_t *child,
						    struct obs_video_info *canvas, uint64_t timestamp,
						    const float *visibility, size_t frames, uint32_t mixers)
{
	struct obs_core_audio *audio = route_audio;
	if (!audio || !parent || !child || !timestamp || !mixers || !frames || obs_source_removed(child))
		return NULL;

	size_t parent_idx = route_node(audio, parent);
	size_t child_idx = route_node(audio, child);
	if (parent_idx == DARRAY_INVALID || child_idx == DARRAY_INVALID)
		return NULL;

	struct obs_audio_route_edge edge = {0};
	edge.child = child_idx;
	edge.next = audio->route_nodes.array[parent_idx].first_edge;
	edge.transition_gain = DARRAY_INVALID;
	edge.canvas = canvas;
	edge.timestamp = timestamp;
	edge.mixers = mixers & parent->audio_mixers;
	if (frames > AUDIO_OUTPUT_FRAMES)
		frames = AUDIO_OUTPUT_FRAMES;
	for (size_t i = 0; i < frames; i++) {
		if (!visibility || visibility[i] != 0.0f)
			edge.gate.bits[i / 64] |= 1ULL << (i % 64);
	}
	audio->route_nodes.array[parent_idx].first_edge = audio->route_edges.num;
	da_push_back(audio->route_edges, &edge);
	return &audio->route_edges.array[audio->route_edges.num - 1];
}

void obs_audio_route_add(obs_source_t *parent, obs_source_t *child, struct obs_video_info *canvas, uint64_t timestamp,
			 const float *visibility, size_t frames, uint32_t mixers)
{
	add_audio_route(parent, child, canvas, timestamp, visibility, frames, mixers);
}

float *obs_audio_route_add_transition(obs_source_t *parent, obs_source_t *child, uint64_t timestamp, size_t frames,
				      uint32_t mixers)
{
	struct obs_audio_route_edge *edge = add_audio_route(parent, child, NULL, timestamp, NULL, frames, mixers);
	if (!edge)
		return NULL;
	edge->transition_gain = route_audio->route_transition_gains.num;
	da_resize(route_audio->route_transition_gains, edge->transition_gain + 1);
	return route_audio->route_transition_gains.array[edge->transition_gain].samples;
}

static void clear_audio_routes(struct obs_core_audio *audio)
{
	for (size_t i = 0; i < audio->route_nodes.num; i++)
		obs_source_release(audio->route_nodes.array[i].source);
	da_resize(audio->route_nodes, 0);
	da_resize(audio->route_edges, 0);
	da_resize(audio->route_transition_gains, 0);
	da_resize(audio->route_roots, 0);
	da_resize(audio->route_pending, 0);
}

void obs_audio_routes_free(struct obs_core_audio *audio)
{
	clear_audio_routes(audio);
	da_free(audio->route_nodes);
	da_free(audio->route_edges);
	da_free(audio->route_roots);
	da_free(audio->route_gates);
	da_free(audio->route_transition_gains);
	da_free(audio->route_gains);
	da_free(audio->route_pending);
	da_free(audio->route_canvases);
}

static void record_composite_route(obs_source_t *parent, obs_source_t *child, void *unused)
{
	obs_audio_route_add(parent, child, NULL, parent->audio_ts, NULL, AUDIO_OUTPUT_FRAMES, parent->audio_mixers);
	UNUSED_PARAMETER(unused);
}

static bool source_has_route_audio(const obs_source_t *source, uint32_t mixer)
{
	return !source->audio_pending && source->audio_ts && (source->audio_mixers & mixer);
}

static bool can_follow_audio_route(const struct obs_audio_route_edge *edge, const obs_source_t *child,
				   const struct obs_video_info *canvas, uint32_t mixer)
{
	return (!edge->canvas || edge->canvas == canvas) && (edge->mixers & mixer) &&
	       source_has_route_audio(child, mixer);
}

static void resolve_audio_routes(struct obs_core_audio *audio, struct obs_video_info *canvas, size_t mix)
{
	da_resize(audio->route_gates, audio->route_nodes.num);
	memset(audio->route_gates.array, 0, audio->route_gates.num * sizeof(*audio->route_gates.array));
	da_resize(audio->route_pending, 0);
	for (size_t i = 0; i < audio->route_roots.num; i++) {
		obs_source_t *source = audio->route_roots.array[i];
		if (!source_has_route_audio(source, 1U << mix))
			continue;
		size_t idx = find_route_node(audio, source);
		if (idx == DARRAY_INVALID)
			continue;
		memset(&audio->route_gates.array[idx], 0xff, sizeof(struct obs_audio_route_gate));
		da_push_back(audio->route_pending, &idx);
	}

	/* Eligibility follows complete output paths. A child rendered under a hidden
	 * parent must not claim the source ahead of another visible parent. */
	for (size_t pending = 0; pending < audio->route_pending.num; pending++) {
		size_t idx = audio->route_pending.array[pending];
		struct obs_audio_route_node *node = &audio->route_nodes.array[idx];
		for (size_t e = node->first_edge; e != DARRAY_INVALID; e = audio->route_edges.array[e].next) {
			const struct obs_audio_route_edge *edge = &audio->route_edges.array[e];
			obs_source_t *child = audio->route_nodes.array[edge->child].source;
			if (!can_follow_audio_route(edge, child, canvas, 1U << mix))
				continue;
			struct obs_audio_route_gate *out = &audio->route_gates.array[edge->child];
			if (obs_audio_route_gate_merge(out, &audio->route_gates.array[idx], &edge->gate,
						       child->audio_ts, node->source->audio_ts, edge->timestamp,
						       audio->samples_per_sec))
				da_push_back(audio->route_pending, &edge->child);
		}
	}
}

struct route_gain_context {
	struct obs_core_audio *audio;
	const struct obs_video_info *canvas;
	size_t leaf;
	uint32_t mixer;
};

/* Memoize gains in the leaf's sample window. Parallel scene paths keep the
 * strongest gain, while the two legs of one transition can add up to unity. */
static const float *audio_route_gain(const struct route_gain_context *context, size_t idx)
{
	struct obs_core_audio *audio = context->audio;
	struct obs_audio_route_node *node = &audio->route_nodes.array[idx];
	float *samples = audio->route_gains.array[idx].samples;
	if (node->gain_resolved)
		return node->gain_audible ? samples : NULL;
	node->gain_resolved = true;
	if (idx == context->leaf) {
		for (size_t i = 0; i < AUDIO_OUTPUT_FRAMES; i++)
			samples[i] = 1.0f;
		node->gain_audible = true;
		return samples;
	}

	const uint64_t leaf_ts = audio->route_nodes.array[context->leaf].source->audio_ts;
	const int64_t parent_offset =
		obs_audio_route_frame_offset(leaf_ts, node->source->audio_ts, audio->samples_per_sec);
	for (size_t e = node->first_edge; e != DARRAY_INVALID; e = audio->route_edges.array[e].next) {
		const struct obs_audio_route_edge *edge = &audio->route_edges.array[e];
		const obs_source_t *child = audio->route_nodes.array[edge->child].source;
		if (!can_follow_audio_route(edge, child, context->canvas, context->mixer))
			continue;
		const int64_t edge_offset =
			obs_audio_route_frame_offset(leaf_ts, edge->timestamp, audio->samples_per_sec);
		const int64_t earlier = parent_offset < edge_offset ? parent_offset : edge_offset;
		const int64_t later = parent_offset > edge_offset ? parent_offset : edge_offset;
		const int64_t first = earlier < 0 ? -earlier : 0;
		const int64_t end = AUDIO_OUTPUT_FRAMES - (later > 0 ? later : 0);
		if (first >= end)
			continue;
		const float *child_gain = audio_route_gain(context, edge->child);
		if (!child_gain)
			continue;
		if (!node->gain_audible) {
			memset(samples, 0, sizeof(struct obs_audio_route_gain));
			node->gain_audible = true;
		}
		const float *gain = edge->transition_gain != DARRAY_INVALID
					    ? audio->route_transition_gains.array[edge->transition_gain].samples
					    : NULL;
		for (int64_t f = first; f < end; f++) {
			const size_t frame = (size_t)(f + edge_offset);
			if (!(edge->gate.bits[frame / 64] & (1ULL << (frame % 64))))
				continue;
			if (gain)
				samples[f] = fminf(1.0f, samples[f] + child_gain[f] * gain[frame]);
			else
				samples[f] = fmaxf(samples[f], child_gain[f]);
		}
	}
	return node->gain_audible ? samples : NULL;
}

static bool resolve_audio_route_gain(const struct route_gain_context *context, float *samples)
{
	struct obs_core_audio *audio = context->audio;
	bool found = false;
	for (size_t i = 0; i < audio->route_nodes.num; i++) {
		audio->route_nodes.array[i].gain_resolved = false;
		audio->route_nodes.array[i].gain_audible = false;
	}
	for (size_t i = 0; i < audio->route_roots.num; i++) {
		obs_source_t *source = audio->route_roots.array[i];
		if (!source_has_route_audio(source, context->mixer))
			continue;
		const size_t idx = find_route_node(audio, source);
		if (idx == DARRAY_INVALID)
			continue;
		const float *gain = audio_route_gain(context, idx);
		if (!gain)
			continue;
		if (!found) {
			memcpy(samples, gain, sizeof(struct obs_audio_route_gain));
			found = true;
		} else {
			for (size_t f = 0; f < AUDIO_OUTPUT_FRAMES; f++)
				samples[f] = fmaxf(samples[f], gain[f]);
		}
	}
	return found;
}

static void push_audio_tree(obs_source_t *parent, obs_source_t *source, void *p)
{
	struct obs_core_audio *audio = p;

	if (da_find(audio->render_order, &source, 0) == DARRAY_INVALID) {
		obs_source_t *s = obs_source_get_ref(source);
		if (s) {
			da_push_back(audio->render_order, &s);
			s->audio_is_duplicated = false;
		}
	}

	UNUSED_PARAMETER(parent);
}

static inline bool is_individual_audio_source(obs_source_t *source)
{
	return source->info.type == OBS_SOURCE_TYPE_INPUT && (source->info.output_flags & OBS_SOURCE_AUDIO) &&
	       !(source->info.output_flags & OBS_SOURCE_COMPOSITE);
}

/*
 * This version of push_audio_tree checks whether any source is an Audio Output Capture source ('Desktop Audio',
 * 'wasapi_output_capture' on Windows, 'pulse_output_capture' on Linux, 'coreaudio_output_capture' on macOS), & if the
 * corresponding device is the monitoring device. It then sets the core audio bool 'prevent_monitoring_duplication' to
 * true, which will silence all monitored sources (unless the Audio Output Capture source is muted).
 * Moreover, it has the purpose of detecting sources which appear several times in the audio tree. They are then tagged
 * as such to avoid their mixing in scenes and transitions and mixed directly as root_nodes.
 */
static void push_audio_tree2(obs_source_t *parent, obs_source_t *source, void *p)
{
	if (obs_source_removed(source))
		return;

	struct obs_core_audio *audio = p;
	size_t idx = da_find(audio->render_order, &source, 0);

	if (idx == DARRAY_INVALID) {
		/* First time we see this source → add to render order */
		obs_source_t *s = obs_source_get_ref(source);
		if (s) {
			da_push_back(audio->render_order, &s);
			s->audio_is_duplicated = false;
		}
	} else {
		/* Source already present in tree → mark as duplicated if applicable */
		obs_source_t *s = audio->render_order.array[idx];
		if (is_individual_audio_source(s) && !s->audio_is_duplicated) {
			if (da_find(audio->root_nodes, &source, 0) == DARRAY_INVALID)
				da_push_back(audio->root_nodes, &source);
			s->audio_is_duplicated = true;
		}
	}
	UNUSED_PARAMETER(parent);
}

static inline size_t convert_time_to_frames(size_t sample_rate, uint64_t t)
{
	return (size_t)util_mul_div64(t, sample_rate, 1000000000ULL);
}

static void mix_audio(struct audio_data_mixes_outputs *mixes,
		      obs_source_t *source, size_t channels, size_t sample_rate,
		      struct ts_info *ts)
{
	size_t total_floats = AUDIO_OUTPUT_FRAMES;
	size_t start_point = 0;

	if (source->audio_ts < ts->start || ts->end <= source->audio_ts)
		return;

	if (source->audio_ts != ts->start) {
		start_point = convert_time_to_frames(sample_rate, source->audio_ts - ts->start);
		if (start_point == AUDIO_OUTPUT_FRAMES)
			return;

		total_floats -= start_point;
	}
	for (size_t canvas_idx = 0; canvas_idx < mixes->outputs.num;
	     canvas_idx++) {
		size_t active_canvas_idx =
			source->info.audio_render ? canvas_idx : 0;

		for (size_t mix_idx = 0; mix_idx < MAX_AUDIO_MIXES; mix_idx++) {
			for (size_t ch = 0; ch < channels; ch++) {
				register float *mix =
					mixes->outputs.array[canvas_idx]
						.output[mix_idx]
						.data[ch];
				register float *aud =
					get_source_audio_output_buf(
						source, active_canvas_idx,
						mix_idx, ch);
				if (!aud) {
					continue;
				}
				register float *end;

				mix += start_point;
				end = aud + total_floats;

				while (aud < end)
					*(mix++) += *(aud++);
			}
		}
	}
}

static void mix_routed_audio(struct obs_core_audio *audio, struct audio_data_mixes_outputs *mixes, size_t channels,
			     size_t sample_rate, uint32_t mixers, struct ts_info *ts)
{
	if (!audio->route_nodes.num)
		return;
	const bool weighted = audio->route_transition_gains.num != 0;
	if (weighted)
		da_resize(audio->route_gains, audio->route_nodes.num);
	for (size_t canvas = 0; canvas < mixes->outputs.num; canvas++) {
		for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
			if (!(mixers & (1U << mix)))
				continue;
			if (!weighted)
				resolve_audio_routes(audio, audio->route_canvases.array[canvas], mix);
			for (size_t i = 0; i < audio->route_nodes.num; i++) {
				obs_source_t *source = audio->route_nodes.array[i].source;
				if (!source->audio_is_duplicated || source->audio_pending || !source->audio_ts ||
				    source->audio_ts < ts->start || source->audio_ts >= ts->end)
					continue;
				size_t pos = convert_time_to_frames(sample_rate, source->audio_ts - ts->start);
				if (pos >= AUDIO_OUTPUT_FRAMES)
					continue;
				struct obs_audio_route_gain gain;
				const struct obs_audio_route_gate *gate = NULL;
				if (weighted) {
					const struct route_gain_context context = {
						audio, audio->route_canvases.array[canvas], i, 1U << mix};
					if (!resolve_audio_route_gain(&context, gain.samples))
						continue;
				} else {
					gate = &audio->route_gates.array[i];
					uint64_t any = 0;
					for (size_t w = 0; w < OBS_AUDIO_ROUTE_WORDS; w++)
						any |= gate->bits[w];
					if (!any)
						continue;
				}
				pthread_mutex_lock(&source->audio_buf_mutex);
				for (size_t ch = 0; ch < channels; ch++) {
					size_t source_canvas = source->info.audio_render ? canvas : 0;
					const float *in = get_source_audio_output_buf(source, source_canvas, mix, ch);
					float *out = mixes->outputs.array[canvas].output[mix].data[ch];
					if (!in)
						continue;
					for (size_t f = 0; f < AUDIO_OUTPUT_FRAMES - pos; f++) {
						if (weighted) {
							if (gain.samples[f] != 0.0f)
								out[pos + f] += in[f] * gain.samples[f];
						} else if (gate->bits[f / 64] & (1ULL << (f % 64))) {
							out[pos + f] += in[f];
						}
					}
				}
				pthread_mutex_unlock(&source->audio_buf_mutex);
			}
		}
	}
}

static bool ignore_audio(obs_source_t *source, size_t channels, size_t sample_rate, uint64_t start_ts)
{
	size_t num_floats = source->audio_input_buf[0].size / sizeof(float);
	const char *name = obs_source_get_name(source);

	if (!source->audio_ts && num_floats) {
#if DEBUG_LAGGED_AUDIO == 1
		blog(LOG_DEBUG, "[src: %s] no timestamp, but audio available?", name);
#endif
		for (size_t ch = 0; ch < channels; ch++)
			deque_pop_front(&source->audio_input_buf[ch], NULL, source->audio_input_buf[0].size);
		source->last_audio_input_buf_size = 0;
		return false;
	}

	if (num_floats) {
		/* round up the number of samples to drop */
		size_t drop = (size_t)util_mul_div64(start_ts - source->audio_ts - 1, sample_rate, 1000000000ULL) + 1;
		if (drop > num_floats)
			drop = num_floats;

#if DEBUG_LAGGED_AUDIO == 1
		blog(LOG_DEBUG, "[src: %s] ignored %" PRIu64 "/%" PRIu64 " samples", name, (uint64_t)drop,
		     (uint64_t)num_floats);
#endif
		for (size_t ch = 0; ch < channels; ch++)
			deque_pop_front(&source->audio_input_buf[ch], NULL, drop * sizeof(float));

		source->last_audio_input_buf_size = 0;
		source->audio_ts += util_mul_div64(drop, 1000000000ULL, sample_rate);
		blog(LOG_DEBUG, "[src: %s] ts lag after ignoring: %" PRIu64, name, start_ts - source->audio_ts);

		/* rounding error, adjust */
		if (source->audio_ts == (start_ts - 1))
			source->audio_ts = start_ts;

		/* source is back in sync */
		if (source->audio_ts >= start_ts)
			return true;
	} else {
#if DEBUG_LAGGED_AUDIO == 1
		blog(LOG_DEBUG, "[src: %s] no samples to ignore! ts = %" PRIu64, name, source->audio_ts);
#endif
	}

	if (!source->audio_pending || num_floats) {
		blog(LOG_WARNING,
		     "Source %s audio is lagging (over by %.02f ms) "
		     "at max audio buffering. Restarting source audio.",
		     name, (start_ts - source->audio_ts) / 1000000.);
	}

	source->audio_pending = true;
	source->audio_ts = 0;
	/* tell the timestamp adjustment code in source_output_audio_data to
	 * reset everything, and hopefully fix the timestamps */
	source->timing_set = false;
	return false;
}

static bool discard_if_stopped(obs_source_t *source, size_t channels)
{
	size_t last_size;
	size_t size;

	last_size = source->last_audio_input_buf_size;
	size = source->audio_input_buf[0].size;

	if (!size)
		return false;

	/* if perpetually pending data, it means the audio has stopped,
	 * so clear the audio data */
	if (last_size == size) {
		if (!source->pending_stop) {
			source->pending_stop = true;
#if DEBUG_AUDIO == 1
			blog(LOG_DEBUG, "doing pending stop trick: '%s'", source->context.name);
#endif
			return false;
		}

		for (size_t ch = 0; ch < channels; ch++)
			deque_pop_front(&source->audio_input_buf[ch], NULL, source->audio_input_buf[ch].size);

		source->pending_stop = false;
		source->audio_ts = 0;
		source->last_audio_input_buf_size = 0;
#if DEBUG_AUDIO == 1
		blog(LOG_DEBUG, "source audio data appears to have "
				"stopped, clearing");
#endif
		return true;
	} else {
		source->last_audio_input_buf_size = size;
		return false;
	}
}

#define MAX_AUDIO_SIZE (AUDIO_OUTPUT_FRAMES * sizeof(float))

static inline void discard_audio(struct obs_core_audio *audio, obs_source_t *source, size_t channels,
				 size_t sample_rate, struct ts_info *ts)
{
	size_t total_floats = AUDIO_OUTPUT_FRAMES;
	size_t size;
	/* debug assert only */
	UNUSED_PARAMETER(audio);

#if DEBUG_AUDIO == 1
	bool is_audio_source = source->info.output_flags & OBS_SOURCE_AUDIO;
#endif

	if (source->info.audio_render) {
		source->audio_ts = 0;
		return;
	}

	if (ts->end <= source->audio_ts) {
#if DEBUG_AUDIO == 1
		blog(LOG_DEBUG,
		     "can't discard, source "
		     "timestamp (%" PRIu64 ") >= "
		     "end timestamp (%" PRIu64 ")",
		     source->audio_ts, ts->end);
#endif
		return;
	}

	if (source->audio_ts < (ts->start - 1)) {
		if (source->audio_pending && source->audio_input_buf[0].size < MAX_AUDIO_SIZE &&
		    discard_if_stopped(source, channels))
			return;

#if DEBUG_AUDIO == 1
		if (is_audio_source) {
			blog(LOG_DEBUG,
			     "can't discard, source "
			     "timestamp (%" PRIu64 ") < "
			     "start timestamp (%" PRIu64 ")",
			     source->audio_ts, ts->start);
		}

		/* ignore_audio should have already run and marked this source
		 * pending, unless we *just* added buffering */
		assert(audio->total_buffering_ticks < audio->max_buffering_ticks || source->audio_pending ||
		       !source->audio_ts || audio->buffering_wait_ticks);
#endif
		return;
	}

	if (source->audio_ts != ts->start && source->audio_ts != (ts->start - 1)) {
		size_t start_point = convert_time_to_frames(sample_rate, source->audio_ts - ts->start);
		if (start_point == AUDIO_OUTPUT_FRAMES) {
#if DEBUG_AUDIO == 1
			if (is_audio_source)
				blog(LOG_DEBUG, "can't discard, start point is "
						"at audio frame count");
#endif
			return;
		}

		total_floats -= start_point;
	}

	size = total_floats * sizeof(float);

	if (source->audio_input_buf[0].size < size) {
		if (discard_if_stopped(source, channels))
			return;

#if DEBUG_AUDIO == 1
		if (is_audio_source)
			blog(LOG_DEBUG, "can't discard, data still pending");
#endif
		source->audio_ts = ts->end;
		return;
	}

	for (size_t ch = 0; ch < channels; ch++)
		deque_pop_front(&source->audio_input_buf[ch], NULL, size);

	source->last_audio_input_buf_size = 0;

#if DEBUG_AUDIO == 1
	if (is_audio_source)
		blog(LOG_DEBUG, "audio discarded, new ts: %" PRIu64, ts->end);
#endif

	source->pending_stop = false;
	source->audio_ts = ts->end;
}

static inline bool audio_buffering_maxed(struct obs_core_audio *audio)
{
	return audio->total_buffering_ticks == audio->max_buffering_ticks;
}

static void set_fixed_audio_buffering(struct obs_core_audio *audio, size_t sample_rate, struct ts_info *ts)
{
	struct ts_info new_ts;
	size_t total_ms;
	int ticks;

	if (audio_buffering_maxed(audio))
		return;

	if (!audio->buffering_wait_ticks)
		audio->buffered_ts = ts->start;

	ticks = audio->max_buffering_ticks - audio->total_buffering_ticks;
	audio->total_buffering_ticks += ticks;

	total_ms = audio->total_buffering_ticks * AUDIO_OUTPUT_FRAMES * 1000 / sample_rate;

	blog(LOG_INFO,
	     "Enabling fixed audio buffering, total "
	     "audio buffering is now %d milliseconds",
	     (int)total_ms);

	new_ts.start =
		audio->buffered_ts - audio_frames_to_ns(sample_rate, audio->buffering_wait_ticks * AUDIO_OUTPUT_FRAMES);

	while (ticks--) {
		const uint64_t cur_ticks = ++audio->buffering_wait_ticks;

		new_ts.end = new_ts.start;
		new_ts.start = audio->buffered_ts - audio_frames_to_ns(sample_rate, cur_ticks * AUDIO_OUTPUT_FRAMES);

#if DEBUG_AUDIO == 1
		blog(LOG_DEBUG, "add buffered ts: %" PRIu64 "-%" PRIu64, new_ts.start, new_ts.end);
#endif

		deque_push_front(&audio->buffered_timestamps, &new_ts, sizeof(new_ts));
	}

	*ts = new_ts;
}

static void add_audio_buffering(struct obs_core_audio *audio, size_t sample_rate, struct ts_info *ts, uint64_t min_ts,
				const char *buffering_name)
{
	struct ts_info new_ts;
	uint64_t offset;
	uint64_t frames;
	size_t total_ms;
	size_t ms;
	int ticks;

	if (audio_buffering_maxed(audio))
		return;

	if (!audio->buffering_wait_ticks)
		audio->buffered_ts = ts->start;

	offset = ts->start - min_ts;
	frames = ns_to_audio_frames(sample_rate, offset);
	ticks = (int)((frames + AUDIO_OUTPUT_FRAMES - 1) / AUDIO_OUTPUT_FRAMES);

	audio->total_buffering_ticks += ticks;

	if (audio->total_buffering_ticks >= audio->max_buffering_ticks) {
		ticks -= audio->total_buffering_ticks - audio->max_buffering_ticks;
		audio->total_buffering_ticks = audio->max_buffering_ticks;
		blog(LOG_WARNING, "Max audio buffering reached!");
	}

	ms = ticks * AUDIO_OUTPUT_FRAMES * 1000 / sample_rate;
	total_ms = audio->total_buffering_ticks * AUDIO_OUTPUT_FRAMES * 1000 / sample_rate;

	blog(LOG_INFO,
	     "adding %d milliseconds of audio buffering, total "
	     "audio buffering is now %d milliseconds"
	     " (source: %s)\n",
	     (int)ms, (int)total_ms, buffering_name);
#if DEBUG_AUDIO == 1
	blog(LOG_DEBUG,
	     "min_ts (%" PRIu64 ") < start timestamp "
	     "(%" PRIu64 ")",
	     min_ts, ts->start);
	blog(LOG_DEBUG, "old buffered ts: %" PRIu64 "-%" PRIu64, ts->start, ts->end);
#endif

	new_ts.start =
		audio->buffered_ts - audio_frames_to_ns(sample_rate, audio->buffering_wait_ticks * AUDIO_OUTPUT_FRAMES);

	while (ticks--) {
		const uint64_t cur_ticks = ++audio->buffering_wait_ticks;

		new_ts.end = new_ts.start;
		new_ts.start = audio->buffered_ts - audio_frames_to_ns(sample_rate, cur_ticks * AUDIO_OUTPUT_FRAMES);

#if DEBUG_AUDIO == 1
		blog(LOG_DEBUG, "add buffered ts: %" PRIu64 "-%" PRIu64, new_ts.start, new_ts.end);
#endif

		deque_push_front(&audio->buffered_timestamps, &new_ts, sizeof(new_ts));
	}

	*ts = new_ts;
}

static bool audio_buffer_insufficient(struct obs_source *source, size_t sample_rate, uint64_t min_ts)
{
	size_t total_floats = AUDIO_OUTPUT_FRAMES;
	size_t size;

	if (source->info.audio_render || source->audio_pending || !source->audio_ts) {
		return false;
	}

	if (source->audio_ts != min_ts && source->audio_ts != (min_ts - 1)) {
		size_t start_point = convert_time_to_frames(sample_rate, source->audio_ts - min_ts);
		if (start_point >= AUDIO_OUTPUT_FRAMES)
			return false;

		total_floats -= start_point;
	}

	size = total_floats * sizeof(float);

	if (source->audio_input_buf[0].size < size) {
		source->audio_pending = true;
		return true;
	}

	return false;
}

static inline const char *find_min_ts(struct obs_core_data *data, uint64_t *min_ts)
{
	obs_source_t *buffering_source = NULL;
	struct obs_source *source = data->first_audio_source;
	while (source) {
		if (!source->audio_pending && source->audio_ts && source->audio_ts < *min_ts) {
			*min_ts = source->audio_ts;
			buffering_source = source;
		}

		source = (struct obs_source *)source->next_audio_source;
	}
	return buffering_source ? obs_source_get_name(buffering_source) : NULL;
}

static inline bool mark_invalid_sources(struct obs_core_data *data, size_t sample_rate, uint64_t min_ts)
{
	bool recalculate = false;

	struct obs_source *source = data->first_audio_source;
	while (source) {
		recalculate |= audio_buffer_insufficient(source, sample_rate, min_ts);
		source = (struct obs_source *)source->next_audio_source;
	}

	return recalculate;
}

static inline const char *calc_min_ts(struct obs_core_data *data, size_t sample_rate, uint64_t *min_ts)
{
	const char *buffering_name = find_min_ts(data, min_ts);
	if (mark_invalid_sources(data, sample_rate, *min_ts))
		buffering_name = find_min_ts(data, min_ts);
	return buffering_name;
}

static inline void release_audio_sources(struct obs_core_audio *audio)
{
	for (size_t i = 0; i < audio->render_order.num; i++)
		obs_source_release(audio->render_order.array[i]);
}

static inline void execute_audio_tasks(void)
{
	struct obs_core_audio *audio = &obs->audio;
	bool tasks_remaining = true;

	while (tasks_remaining) {
		pthread_mutex_lock(&audio->task_mutex);
		if (audio->tasks.size) {
			struct obs_task_info info;
			deque_pop_front(&audio->tasks, &info, sizeof(info));
			info.task(info.param);
		}
		tasks_remaining = !!audio->tasks.size;
		pthread_mutex_unlock(&audio->task_mutex);
	}
}

/* In case monitoring and an 'Audio Output Capture' source have the same device, one silences all the monitored
 * sources unless the 'Audio Output Capture' is muted.
 */
static inline bool should_silence_monitored_source(obs_source_t *source, obs_source_t *dup_src)
{
	if (!dup_src || !obs_source_active(dup_src))
		return false;

	if (dup_src->monitoring_type == OBS_MONITORING_TYPE_MONITOR_ONLY)
		return false;

	bool fader_muted = close_float(dup_src->volume, 0.0f, 0.0001f);
	bool output_capture_unmuted = !dup_src->muted && !fader_muted;

	if (output_capture_unmuted) {
		if (source->monitoring_type == OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT && source != dup_src) {
			return true;
		}
	}
	return false;
}

static inline void clear_audio_output_buf(obs_source_t *source, obs_source_t *dup_src)
{
	if (!dup_src)
		return;

	uint32_t aoc_mixers = dup_src->audio_mixers;
	uint32_t source_mixers = source->audio_mixers;

	for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
		uint32_t mix_and_val = (1 << mix);
		if ((aoc_mixers & mix_and_val) && (source_mixers & mix_and_val)) {
			for (size_t canvas_idx = 0;
			     canvas_idx < source->audio_output_bufs.num;
			     canvas_idx++) {
				for (size_t ch = 0; ch < MAX_AUDIO_CHANNELS;
				     ch++) {
					float *buf = get_source_audio_output_buf(
						source, canvas_idx, mix, ch);
					if (buf) {
						memset(buf, 0,
						       AUDIO_OUTPUT_FRAMES *
							       sizeof(float));
					}
				}
			}
		}
	}
}

bool audio_callback(void *param, uint64_t start_ts_in, uint64_t end_ts_in, uint64_t *out_ts, uint32_t mixers,
		    struct audio_data_mixes_outputs *mixes)
{
	struct obs_core_data *data = &obs->data;
	struct obs_core_audio *audio = param;
	struct obs_source *source;
	size_t sample_rate = audio->samples_per_sec;
	size_t channels = audio->channels;
	struct ts_info ts = {start_ts_in, end_ts_in};
	size_t audio_size;
	uint64_t min_ts;

	da_resize(audio->render_order, 0);
	da_resize(audio->root_nodes, 0);
	clear_audio_routes(audio);

	/* ------------------------------------------------ */
	/* build audio render order */
	pthread_mutex_lock(&obs->video.mixes_mutex);
	for (size_t j = 0; j < obs->video.mixes.num; j++) {
		struct obs_view *view = obs->video.mixes.array[j]->view;
		if (!view)
			continue;

		pthread_mutex_lock(&view->channels_mutex);

		/* NOTE: these are source channels, not audio channels */
		for (uint32_t i = 0; i < MAX_CHANNELS; i++) {
			obs_source_t *source = view->channels[i];
			if (!source)
				continue;
			if (!obs_source_active(source))
				continue;
			if (obs_source_removed(source))
				continue;

			/* first, add top - level sources as root_nodes */
			if (obs->video.mixes.array[j]->mix_audio) {
				if (da_find(audio->root_nodes, &source, 0) == DARRAY_INVALID)
					da_push_back(audio->root_nodes, &source);
				if (da_find(audio->route_roots, &source, 0) == DARRAY_INVALID)
					da_push_back(audio->route_roots, &source);
			}

			/* Build audio tree, tag duplicate individual sources */
			obs_source_enum_active_tree(source, push_audio_tree2, audio);

			/* add top - level sources to audio tree */
			push_audio_tree(NULL, source, audio);
		}
		pthread_mutex_unlock(&view->channels_mutex);
	}
	pthread_mutex_unlock(&obs->video.mixes_mutex);

	pthread_mutex_lock(&data->audio_sources_mutex);

	deque_push_back(&audio->buffered_timestamps, &ts, sizeof(ts));
	deque_peek_front(&audio->buffered_timestamps, &ts, sizeof(ts));
	min_ts = ts.start;

	audio_size = AUDIO_OUTPUT_FRAMES * sizeof(float);

#if DEBUG_AUDIO == 1
	blog(LOG_DEBUG, "ts %llu-%llu", ts.start, ts.end);
#endif

	source = data->first_audio_source;
	while (source) {
		if (!obs_source_removed(source)) {
			push_audio_tree(NULL, source, audio);
		}
		source = (struct obs_source *)source->next_audio_source;
	}

	pthread_mutex_unlock(&data->audio_sources_mutex);

	/* ------------------------------------------------ */
	/* render audio data */
	/* Scene actions can change visible/active state during rendering. Retain
	 * their sample windows now instead of rescanning the resulting graph. */
	for (size_t i = 0; i < audio->render_order.num; i++) {
		if (audio->render_order.array[i]->audio_is_duplicated) {
			route_audio = audio;
			break;
		}
	}
	struct obs_monitoring_deduplication *deduplication = &obs->monitoring_deduplication;
	pthread_mutex_lock(&deduplication->mutex);
	obs_source_t *dup_src = obs_weak_source_get_source(deduplication->source);
	pthread_mutex_unlock(&deduplication->mutex);

	for (size_t i = 0; i < audio->render_order.num; i++) {
		obs_source_t *source = audio->render_order.array[i];
		obs_source_audio_render(source, mixers, channels, sample_rate, audio_size);
		if (route_audio && source->info.type != OBS_SOURCE_TYPE_SCENE &&
		    source->info.type != OBS_SOURCE_TYPE_TRANSITION && !obs_source_is_group(source))
			obs_source_enum_active_sources(source, record_composite_route, NULL);
		if (should_silence_monitored_source(source, dup_src))
			clear_audio_output_buf(source, dup_src);

		/* if a source has gone backward in time and we can no
		 * longer buffer, drop some or all of its audio */
		if (audio_buffering_maxed(audio) && source->audio_ts != 0 && source->audio_ts < ts.start) {
			if (source->info.audio_render) {
				blog(LOG_DEBUG,
				     "render audio source %s timestamp has "
				     "gone backwards",
				     obs_source_get_name(source));

				/* just avoid further damage */
				source->audio_pending = true;
#if DEBUG_AUDIO == 1
				/* this should really be fixed */
				assert(false);
#endif
			} else {
				pthread_mutex_lock(&source->audio_buf_mutex);
				bool rerender = ignore_audio(source, channels, sample_rate, ts.start);
				pthread_mutex_unlock(&source->audio_buf_mutex);

				/* if we (potentially) recovered, re-render */
				if (rerender)
					obs_source_audio_render(source, mixers, channels, sample_rate, audio_size);
			}
		}
	}
	bool routed = route_audio != NULL;
	route_audio = NULL;
	if (routed) {
		for (size_t i = 0; i < audio->route_roots.num; i++)
			route_node(audio, audio->route_roots.array[i]);
		da_resize(audio->route_canvases, mixes->outputs.num);
		pthread_mutex_lock(&obs->video.canvases_mutex);
		for (size_t i = 0; i < mixes->outputs.num; i++)
			audio->route_canvases.array[i] = i < obs->video.canvases.num ? obs->video.canvases.array[i]
										     : NULL;
		pthread_mutex_unlock(&obs->video.canvases_mutex);
	}
	obs_source_release(dup_src);

	/* ------------------------------------------------ */
	/* get minimum audio timestamp */
	pthread_mutex_lock(&data->audio_sources_mutex);
	const char *buffering_name = calc_min_ts(data, sample_rate, &min_ts);
	pthread_mutex_unlock(&data->audio_sources_mutex);

	/* ------------------------------------------------ */
	/* if a source has gone backward in time, buffer    */
	if (audio->fixed_buffer) {
		if (!audio_buffering_maxed(audio)) {
			set_fixed_audio_buffering(audio, sample_rate, &ts);
		}
	} else if (min_ts < ts.start) {
		add_audio_buffering(audio, sample_rate, &ts, min_ts, buffering_name);
	}

	/* ------------------------------------------------ */
	/* mix audio */
	if (!audio->buffering_wait_ticks) {
		for (size_t i = 0; i < audio->root_nodes.num; i++) {
			obs_source_t *source = audio->root_nodes.array[i];

			if (source->audio_pending || source->audio_is_duplicated)
				continue;

			pthread_mutex_lock(&source->audio_buf_mutex);

			if (get_source_audio_output_buf(source, 0, 0, 0) &&
			    source->audio_ts)
				mix_audio(mixes, source, channels, sample_rate,
					  &ts);

			pthread_mutex_unlock(&source->audio_buf_mutex);
		}
		if (routed)
			mix_routed_audio(audio, mixes, channels, sample_rate, mixers, &ts);
	}

	/* ------------------------------------------------ */
	/* discard audio */
	pthread_mutex_lock(&data->audio_sources_mutex);

	source = data->first_audio_source;
	while (source) {
		pthread_mutex_lock(&source->audio_buf_mutex);
		discard_audio(audio, source, channels, sample_rate, &ts);
		pthread_mutex_unlock(&source->audio_buf_mutex);

		source = (struct obs_source *)source->next_audio_source;
	}

	pthread_mutex_unlock(&data->audio_sources_mutex);

	/* ------------------------------------------------ */
	/* release audio sources */
	clear_audio_routes(audio);
	release_audio_sources(audio);

	deque_pop_front(&audio->buffered_timestamps, NULL, sizeof(ts));

	*out_ts = ts.start;

	if (audio->buffering_wait_ticks) {
		audio->buffering_wait_ticks--;
		return false;
	}

	execute_audio_tasks();

	UNUSED_PARAMETER(param);
	return true;
}
