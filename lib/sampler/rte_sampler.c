/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <string.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_log.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>
#include <rte_sampler.h>

/* Initial capacities for dynamic arrays */
#define INITIAL_SESSIONS_CAPACITY 32
#define INITIAL_SOURCES_PER_SESSION 8
#define INITIAL_SINKS_PER_SESSION 4

/**
 * Sampler source structure
 */
struct rte_sampler_source {
	struct rte_sampler_session *session;  /**< Back-pointer to session */
	char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
	uint16_t source_id;
	struct rte_sampler_source_ops ops;
	void *user_data;
	struct rte_sampler_xstats_name *xstats_names;  /**< Dynamically allocated */
	uint64_t *ids;                                  /**< Dynamically allocated */
	uint64_t *values;                               /**< Dynamically allocated */
	unsigned int xstats_count;
	unsigned int xstats_capacity;                   /**< Allocated capacity */
/* Filter support */
	char **filter_patterns;                         /**< Dynamically allocated array */
	unsigned int num_filter_patterns;
	unsigned int filter_patterns_capacity;          /**< Allocated capacity */
	uint64_t *filtered_ids;                         /**< Dynamically allocated */
	unsigned int filtered_count;
	uint8_t filter_active;
	uint8_t valid;
};

/**
 * Sampler sink structure
 */
struct rte_sampler_sink {
	struct rte_sampler_session *session;  /**< Back-pointer to session */
	char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
	struct rte_sampler_sink_ops ops;
	void *user_data;
	uint8_t valid;
};

/**
 * Sampler session structure
 */
struct rte_sampler_session {
	char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
	uint64_t sample_interval_ms;
	uint64_t duration_ms;
	uint64_t start_time;
	uint64_t last_sample_time;
	uint8_t active;
	uint8_t valid;
	struct rte_sampler_source *sources;   /**< Dynamically allocated array */
	struct rte_sampler_sink *sinks;       /**< Dynamically allocated array */
	unsigned int num_sources;
	unsigned int num_sinks;
	unsigned int sources_capacity;        /**< Allocated capacity */
	unsigned int sinks_capacity;          /**< Allocated capacity */
};

/**
 * Global session registry
 */
static struct {
	struct rte_sampler_session **sessions;  /**< Dynamically allocated array */
	unsigned int num_sessions;
	unsigned int capacity;                  /**< Allocated capacity */
} sampler_global = {
	.sessions = NULL,
	.num_sessions = 0,
	.capacity = 0
};

/* Forward declarations */
static void apply_filter(struct rte_sampler_source *source);
static int matches_filter(struct rte_sampler_source *source, const char *name);
static int match_pattern(const char *pattern, const char *str);


RTE_EXPORT_SYMBOL(rte_sampler_session_create)
struct rte_sampler_session *
		rte_sampler_session_create(const struct rte_sampler_session_conf *conf)
{
	struct rte_sampler_session *session;
	unsigned int i;

	session = rte_zmalloc(NULL, sizeof(struct rte_sampler_session),
		RTE_CACHE_LINE_SIZE);
	if (session == NULL)
		return NULL;

	/* Allocate initial capacity for sources and sinks */
	session->sources = rte_zmalloc(NULL,
		INITIAL_SOURCES_PER_SESSION * sizeof(struct rte_sampler_source),
		RTE_CACHE_LINE_SIZE);
	if (session->sources == NULL) {
		rte_free(session);
		return NULL;
	}
	session->sources_capacity = INITIAL_SOURCES_PER_SESSION;

	session->sinks = rte_zmalloc(NULL,
		INITIAL_SINKS_PER_SESSION * sizeof(struct rte_sampler_sink),
		RTE_CACHE_LINE_SIZE);
	if (session->sinks == NULL) {
		rte_free(session->sources);
		rte_free(session);
		return NULL;
	}
	session->sinks_capacity = INITIAL_SINKS_PER_SESSION;

	/* Set configuration */
	if (conf != NULL) {
		session->sample_interval_ms = conf->sample_interval_ms;
		session->duration_ms = conf->duration_ms;
		if (conf->name != NULL)
			rte_strscpy(session->name, conf->name, sizeof(session->name));
		else
			snprintf(session->name, sizeof(session->name), "session_%p", session);
	} else {
		/* Default configuration: manual sampling, infinite duration */
		session->sample_interval_ms = 0;
		session->duration_ms = 0;
		snprintf(session->name, sizeof(session->name), "session_%p", session);
	}

	session->valid = 1;

	/* Register session globally - grow array if needed */
	if (sampler_global.sessions == NULL) {
		/* First time initialization */
		sampler_global.sessions = rte_zmalloc(NULL,
			INITIAL_SESSIONS_CAPACITY * sizeof(struct rte_sampler_session *),
			RTE_CACHE_LINE_SIZE);
		if (sampler_global.sessions == NULL) {
			rte_free(session->sinks);
			rte_free(session->sources);
			rte_free(session);
			return NULL;
		}
		sampler_global.capacity = INITIAL_SESSIONS_CAPACITY;
	}

	/* Find free slot or grow array */
	for (i = 0; i < sampler_global.capacity; i++) {
		if (sampler_global.sessions[i] == NULL) {
			sampler_global.sessions[i] = session;
			if (i >= sampler_global.num_sessions)
				sampler_global.num_sessions = i + 1;
			break;
		}
	}

	if (i >= sampler_global.capacity) {
		/* Need to grow the array */
		struct rte_sampler_session **new_sessions;
		unsigned int new_capacity = sampler_global.capacity * 2;

		new_sessions = rte_zmalloc(NULL,
			new_capacity * sizeof(struct rte_sampler_session *),
			RTE_CACHE_LINE_SIZE);
		if (new_sessions == NULL) {
			RTE_LOG(WARNING, USER1,
				"Failed to grow session registry, session will not be polled automatically\n");
			/* Session still created, just not registered for polling */
			return session;
		}

		/* Copy old sessions */
		for (i = 0; i < sampler_global.capacity; i++)
			new_sessions[i] = sampler_global.sessions[i];

		/* Free old array and use new one */
		rte_free(sampler_global.sessions);
		sampler_global.sessions = new_sessions;

		/* Add new session */
		sampler_global.sessions[sampler_global.capacity] = session;
		sampler_global.num_sessions = sampler_global.capacity + 1;
		sampler_global.capacity = new_capacity;
	}

	return session;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_free)
void
		rte_sampler_session_free(struct rte_sampler_session *session)
{
	unsigned int i;

	if (session == NULL)
		return;

	/* Stop session if active */
	if (session->active)
		rte_sampler_session_stop(session);

	/* Free all sources */
	if (session->sources != NULL) {
		for (i = 0; i < session->num_sources; i++) {
			struct rte_sampler_source *source = &session->sources[i];
			if (source->valid) {
				/* Free dynamic arrays in source */
				rte_free(source->xstats_names);
				rte_free(source->ids);
				rte_free(source->values);
				rte_free(source->filtered_ids);
				/* Free filter patterns */
				if (source->filter_patterns != NULL) {
					unsigned int j;
					for (j = 0; j < source->num_filter_patterns; j++)
						rte_free(source->filter_patterns[j]);
					rte_free(source->filter_patterns);
				}
			}
		}
		rte_free(session->sources);
	}

	/* Free all sinks */
	rte_free(session->sinks);

	/* Unregister from global registry */
	for (i = 0; i < sampler_global.num_sessions; i++) {
		if (sampler_global.sessions != NULL &&
		    sampler_global.sessions[i] == session) {
			sampler_global.sessions[i] = NULL;
			break;
		}
	}

	session->valid = 0;
	rte_free(session);
}

RTE_EXPORT_SYMBOL(rte_sampler_session_start)
int
		rte_sampler_session_start(struct rte_sampler_session *session)
{
if (session == NULL || !session->valid)
return -EINVAL;

session->active = 1;
session->start_time = rte_get_timer_cycles();
session->last_sample_time = session->start_time;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_stop)
int
		rte_sampler_session_stop(struct rte_sampler_session *session)
{
if (session == NULL || !session->valid)
return -EINVAL;

session->active = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_is_active)
int
		rte_sampler_session_is_active(struct rte_sampler_session *session)
{
uint64_t current_time, elapsed_ms;

if (session == NULL || !session->valid)
return -EINVAL;

if (!session->active)
return 0;

/* Check if duration has expired */
if (session->duration_ms > 0) {
current_time = rte_get_timer_cycles();
elapsed_ms = (current_time - session->start_time) * 1000 /
     rte_get_timer_hz();
if (elapsed_ms >= session->duration_ms) {
session->active = 0;
return 0;
}
}

return 1;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_free)
void
		rte_sampler_source_free(struct rte_sampler_source *source)
{
if (source == NULL)
return;

source->valid = 0;
/* Note: source is part of session structure, not separately allocated */
}

RTE_EXPORT_SYMBOL(rte_sampler_source_register)
struct rte_sampler_source *
		rte_sampler_source_register(struct rte_sampler_session *session,
		const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_source_ops *ops,
		void *user_data)
{
	struct rte_sampler_source *source;
	unsigned int i;

	if (session == NULL || !session->valid ||
	    source_name == NULL || ops == NULL)
		return NULL;

	if (ops->xstats_names_get == NULL || ops->xstats_get == NULL)
		return NULL;

	/* Find free slot or grow array */
	for (i = 0; i < session->sources_capacity; i++) {
		if (!session->sources[i].valid)
			break;
	}

	if (i >= session->sources_capacity) {
		/* Need to grow the array */
		struct rte_sampler_source *new_sources;
		unsigned int new_capacity = session->sources_capacity * 2;

		new_sources = rte_zmalloc(NULL,
			new_capacity * sizeof(struct rte_sampler_source),
			RTE_CACHE_LINE_SIZE);
		if (new_sources == NULL)
			return NULL;

		/* Copy old sources */
		memcpy(new_sources, session->sources,
			session->sources_capacity * sizeof(struct rte_sampler_source));

		/* Free old array and use new one */
		rte_free(session->sources);
		session->sources = new_sources;
		session->sources_capacity = new_capacity;
	}

	source = &session->sources[i];
	memset(source, 0, sizeof(*source));

	source->session = session;
	rte_strscpy(source->name, source_name, sizeof(source->name));
	source->source_id = source_id;
	source->ops = *ops;
	source->user_data = user_data;
	source->valid = 1;

	if (i >= session->num_sources)
		session->num_sources = i + 1;

	return source;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_unregister)
int
		rte_sampler_source_unregister(struct rte_sampler_source *source)
{
if (source == NULL || !source->valid)
return -EINVAL;

source->valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_free)
void
		rte_sampler_sink_free(struct rte_sampler_sink *sink)
{
if (sink == NULL)
return;

sink->valid = 0;
/* Note: sink is part of session structure, not separately allocated */
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_register)
struct rte_sampler_sink *
		rte_sampler_sink_register(struct rte_sampler_session *session,
		const char *sink_name,
		const struct rte_sampler_sink_ops *ops,
		void *user_data)
{
	struct rte_sampler_sink *sink;
	unsigned int i;

	if (session == NULL || !session->valid ||
	    sink_name == NULL || ops == NULL)
		return NULL;

	if (ops->output == NULL)
		return NULL;

	/* Find free slot or grow array */
	for (i = 0; i < session->sinks_capacity; i++) {
		if (!session->sinks[i].valid)
			break;
	}

	if (i >= session->sinks_capacity) {
		/* Need to grow the array */
		struct rte_sampler_sink *new_sinks;
		unsigned int new_capacity = session->sinks_capacity * 2;

		new_sinks = rte_zmalloc(NULL,
			new_capacity * sizeof(struct rte_sampler_sink),
			RTE_CACHE_LINE_SIZE);
		if (new_sinks == NULL)
			return NULL;

		/* Copy old sinks */
		memcpy(new_sinks, session->sinks,
			session->sinks_capacity * sizeof(struct rte_sampler_sink));

		/* Free old array and use new one */
		rte_free(session->sinks);
		session->sinks = new_sinks;
		session->sinks_capacity = new_capacity;
	}

	sink = &session->sinks[i];
	memset(sink, 0, sizeof(*sink));

	sink->session = session;
	rte_strscpy(sink->name, sink_name, sizeof(sink->name));
	sink->ops = *ops;
	sink->user_data = user_data;
	sink->valid = 1;

	if (i >= session->num_sinks)
		session->num_sinks = i + 1;

	return sink;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_unregister)
int
		rte_sampler_sink_unregister(struct rte_sampler_sink *sink)
{
if (sink == NULL || !sink->valid)
return -EINVAL;

sink->valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sample)
int
		rte_sampler_sample(struct rte_sampler_session *session)
{
	unsigned int i, j;
	int ret;

	if (session == NULL || !session->valid)
		return -EINVAL;

	/* Sample from all sources */
	for (i = 0; i < session->num_sources; i++) {
		struct rte_sampler_source *source = &session->sources[i];

		if (!source->valid)
			continue;

		/* Get xstats names if not already cached */
		if (source->xstats_count == 0) {
			/* First, query the size */
			ret = source->ops.xstats_names_get(
				source->source_id,
				NULL,
				NULL,
				0,
				source->user_data);
			if (ret < 0)
				continue;

			if (ret == 0)
				continue;

			/* Allocate arrays based on actual size needed */
			source->xstats_capacity = ret;
			source->xstats_names = rte_zmalloc(NULL,
				ret * sizeof(struct rte_sampler_xstats_name),
				RTE_CACHE_LINE_SIZE);
			if (source->xstats_names == NULL)
				continue;

			source->ids = rte_zmalloc(NULL,
				ret * sizeof(uint64_t),
				RTE_CACHE_LINE_SIZE);
			if (source->ids == NULL) {
				rte_free(source->xstats_names);
				source->xstats_names = NULL;
				continue;
			}

			source->values = rte_zmalloc(NULL,
				ret * sizeof(uint64_t),
				RTE_CACHE_LINE_SIZE);
			if (source->values == NULL) {
				rte_free(source->xstats_names);
				rte_free(source->ids);
				source->xstats_names = NULL;
				source->ids = NULL;
				continue;
			}

			source->filtered_ids = rte_zmalloc(NULL,
				ret * sizeof(uint64_t),
				RTE_CACHE_LINE_SIZE);
			if (source->filtered_ids == NULL) {
				rte_free(source->xstats_names);
				rte_free(source->ids);
				rte_free(source->values);
				source->xstats_names = NULL;
				source->ids = NULL;
				source->values = NULL;
				continue;
			}

			/* Now get the actual names and IDs */
			ret = source->ops.xstats_names_get(
				source->source_id,
				source->xstats_names,
				source->ids,
				source->xstats_capacity,
				source->user_data);
			if (ret < 0) {
				rte_free(source->xstats_names);
				rte_free(source->ids);
				rte_free(source->values);
				rte_free(source->filtered_ids);
				source->xstats_names = NULL;
				source->ids = NULL;
				source->values = NULL;
				source->filtered_ids = NULL;
				source->xstats_capacity = 0;
				continue;
			}

			source->xstats_count = ret;

			/* Apply filter to determine which stats to sample */
			apply_filter(source);
		}

		/* Get xstats values (using filtered IDs if filter is active) */
		if (source->filtered_count > 0) {
			ret = source->ops.xstats_get(
				source->source_id,
				source->filtered_ids,
				source->values,
				source->filtered_count,
				source->user_data);
			if (ret < 0)
				continue;
		}

		/* Send to all sinks */
		for (j = 0; j < session->num_sinks; j++) {
			struct rte_sampler_sink *sink = &session->sinks[j];
			const struct rte_sampler_xstats_name *names_to_pass;

if (!sink->valid)
continue;

/* Optimization: don't pass names if sink doesn't want them */
if (sink->ops.flags & RTE_SAMPLER_SINK_F_NO_NAMES)
names_to_pass = NULL;
else
names_to_pass = source->xstats_names;

ret = sink->ops.output(
source->name,
source->source_id,
names_to_pass,
source->filtered_ids,
source->values,
source->filtered_count,
sink->user_data);
if (ret < 0) {
/* Continue with other sinks on error */
continue;
}
}
}

/* Update last sample time */
session->last_sample_time = rte_get_timer_cycles();

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_poll)
int
rte_sampler_poll(void)
{
unsigned int i;
int polled = 0;
uint64_t current_time, elapsed_ms;

for (i = 0; i < sampler_global.num_sessions; i++) {
struct rte_sampler_session *session = sampler_global.sessions[i];

if (session == NULL || !session->valid)
continue;

/* Skip inactive sessions */
if (!rte_sampler_session_is_active(session))
continue;

/* Skip manual sessions (interval == 0) */
if (session->sample_interval_ms == 0)
continue;

/* Check if it's time to sample */
current_time = rte_get_timer_cycles();
elapsed_ms = (current_time - session->last_sample_time) * 1000 /
     rte_get_timer_hz();

if (elapsed_ms >= session->sample_interval_ms) {
rte_sampler_sample(session);
polled++;
}
}

return polled;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_names_get)
int
		rte_sampler_xstats_names_get(struct rte_sampler_session *session,
		struct rte_sampler_source *source,
		struct rte_sampler_xstats_name *xstats_names,
		unsigned int size)
{
unsigned int total = 0;
unsigned int i, j;

if (session == NULL || !session->valid)
return -EINVAL;

/* Get from specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

if (xstats_names == NULL)
return source->xstats_count;

for (i = 0; i < source->xstats_count && i < size; i++) {
xstats_names[i] = source->xstats_names[i];
}

return i;
}

/* Get from all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

if (xstats_names != NULL) {
for (j = 0; j < src->xstats_count && 
     total + j < size; j++) {
xstats_names[total + j] = src->xstats_names[j];
}
}

total += src->xstats_count;
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_get)
int
		rte_sampler_xstats_get(struct rte_sampler_session *session,
		struct rte_sampler_source *source,
		const uint64_t *ids,
		uint64_t *values,
		unsigned int n)
{
unsigned int total = 0;
unsigned int i, j;

if (session == NULL || !session->valid || values == NULL)
return -EINVAL;

/* Get from specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

/* If ids is NULL, return all values */
if (ids == NULL) {
for (i = 0; i < source->xstats_count && i < n; i++) {
values[i] = source->values[i];
}
return i;
}

/* Get specific ids */
for (i = 0; i < n; i++) {
for (j = 0; j < source->xstats_count; j++) {
if (source->ids[j] == ids[i]) {
values[i] = source->values[j];
break;
}
}
}

return n;
}

/* Get from all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

for (j = 0; j < src->xstats_count; j++) {
if (total >= n)
break;
values[total++] = src->values[j];
}
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_reset)
int
		rte_sampler_xstats_reset(struct rte_sampler_session *session,
		struct rte_sampler_source *source,
		const uint64_t *ids,
		unsigned int n)
{
unsigned int i;
int ret;

if (session == NULL || !session->valid)
return -EINVAL;

/* Reset specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

if (source->ops.xstats_reset != NULL) {
ret = source->ops.xstats_reset(
source->source_id,
ids,
n,
source->user_data);
if (ret < 0)
return ret;
}

/* Clear cached values */
if (source->values != NULL && source->xstats_capacity > 0)
	memset(source->values, 0, source->xstats_capacity * sizeof(uint64_t));

return 0;
}

/* Reset all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

if (src->ops.xstats_reset != NULL) {
ret = src->ops.xstats_reset(
src->source_id,
ids,
n,
src->user_data);
/* Continue with other sources on error */
}

/* Clear cached values */
if (src->values != NULL && src->xstats_capacity > 0)
	memset(src->values, 0, src->xstats_capacity * sizeof(uint64_t));
}

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_get_xstats_name)
int
		rte_sampler_source_get_xstats_name(struct rte_sampler_source *source,
		uint64_t id,
		struct rte_sampler_xstats_name *name)
{
unsigned int i;

if (source == NULL || !source->valid || name == NULL)
return -EINVAL;

/* Search for the ID in the source's cached stats */
for (i = 0; i < source->xstats_count; i++) {
if (source->ids[i] == id) {
*name = source->xstats_names[i];
return 0;
}
}

return -ENOENT;
}

/**
 * Simple wildcard pattern matching
 * Supports * (match any) and ? (match one character)
 */
static int
		match_pattern(const char *pattern, const char *str)
{
while (*pattern && *str) {
if (*pattern == '*') {
/* Skip consecutive asterisks */
while (*(pattern + 1) == '*')
pattern++;

/* If asterisk is last character, match */
if (!*(pattern + 1))
return 1;

/* Try matching rest of pattern with rest of string */
while (*str) {
if (match_pattern(pattern + 1, str))
return 1;
str++;
}
return 0;
} else if (*pattern == '?' || *pattern == *str) {
pattern++;
str++;
} else {
return 0;
}
}

/* Handle trailing asterisks */
while (*pattern == '*')
pattern++;

return (*pattern == '\0' && *str == '\0');
}

/**
 * Check if a stat name matches any filter pattern
 */
static int
		matches_filter(struct rte_sampler_source *source, const char *name)
{
unsigned int i;

if (!source->filter_active || source->num_filter_patterns == 0)
return 1;  /* No filter, include all */

for (i = 0; i < source->num_filter_patterns; i++) {
if (match_pattern(source->filter_patterns[i], name))
return 1;
}

return 0;
}

/**
 * Apply filter to cached xstats
 */
static void
		apply_filter(struct rte_sampler_source *source)
{
unsigned int i, j;

if (!source->filter_active) {
source->filtered_count = source->xstats_count;
for (i = 0; i < source->xstats_count; i++)
source->filtered_ids[i] = source->ids[i];
return;
}

j = 0;
for (i = 0; i < source->xstats_count; i++) {
if (matches_filter(source, source->xstats_names[i].name)) {
source->filtered_ids[j++] = source->ids[i];
}
}
source->filtered_count = j;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_set_filter)
int
		rte_sampler_source_set_filter(struct rte_sampler_source *source,
		const char **patterns,
		unsigned int num_patterns)
{
	unsigned int i;

	if (source == NULL || !source->valid)
		return -EINVAL;

	if (patterns == NULL || num_patterns == 0)
		return -EINVAL;

	/* Clear old patterns */
	for (i = 0; i < source->num_filter_patterns; i++) {
		rte_free(source->filter_patterns[i]);
		source->filter_patterns[i] = NULL;
	}

	/* Grow filter_patterns array if needed */
	if (num_patterns > source->filter_patterns_capacity) {
		char **new_patterns = rte_zmalloc(NULL,
			num_patterns * sizeof(char *),
			RTE_CACHE_LINE_SIZE);
		if (new_patterns == NULL)
			return -ENOMEM;

		/* Copy old array pointers if any */
		if (source->filter_patterns != NULL) {
			rte_free(source->filter_patterns);
		}
		source->filter_patterns = new_patterns;
		source->filter_patterns_capacity = num_patterns;
	}

	/* Copy new patterns */
	source->num_filter_patterns = 0;
	for (i = 0; i < num_patterns; i++) {
		size_t len = strlen(patterns[i]) + 1;
		source->filter_patterns[i] = rte_malloc(NULL, len, 0);
		if (source->filter_patterns[i] == NULL) {
			/* Cleanup on failure */
			rte_sampler_source_clear_filter(source);
			return -ENOMEM;
		}
		rte_strscpy(source->filter_patterns[i], patterns[i], len);
		source->num_filter_patterns++;
	}

	source->filter_active = 1;

	/* Apply filter to existing stats */
	if (source->xstats_count > 0)
		apply_filter(source);

	return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_clear_filter)
int
		rte_sampler_source_clear_filter(struct rte_sampler_source *source)
{
unsigned int i;

if (source == NULL || !source->valid)
return -EINVAL;

/* Free all patterns */
for (i = 0; i < source->num_filter_patterns; i++) {
rte_free(source->filter_patterns[i]);
source->filter_patterns[i] = NULL;
}

source->num_filter_patterns = 0;
source->filter_active = 0;
source->filtered_count = source->xstats_count;

/* Reset filtered IDs to include all */
for (i = 0; i < source->xstats_count; i++)
source->filtered_ids[i] = source->ids[i];

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_get_filter)
int
		rte_sampler_source_get_filter(struct rte_sampler_source *source,
       char **patterns,
		unsigned int max_patterns)
{
unsigned int i, count;

if (source == NULL || !source->valid)
return -EINVAL;

if (!source->filter_active)
return 0;

count = (max_patterns < source->num_filter_patterns) ?
max_patterns : source->num_filter_patterns;

if (patterns != NULL) {
for (i = 0; i < count; i++) {
patterns[i] = source->filter_patterns[i];
}
}

return source->num_filter_patterns;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_get_xstats_count)
int
		rte_sampler_source_get_xstats_count(struct rte_sampler_source *source)
{
	if (source == NULL || !source->valid)
		return -EINVAL;

	/* If filter is active, return filtered count, otherwise return total count */
	if (source->filter_active)
		return source->filtered_count;
	else
		return source->xstats_count;
}
