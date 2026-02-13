/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <string.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_cycles.h>
#include <rte_spinlock.h>
#include <rte_sampler.h>
#include "rte_sampler_sink_ringbuffer.h"

/**
 * Internal ring buffer entry
 */
struct ringbuffer_entry {
	uint64_t timestamp;
	char source_name[64];
	uint16_t source_id;
	uint16_t num_stats;
	uint64_t *ids;
	uint64_t *values;
	uint8_t valid;
};

/**
 * Ring buffer sink data structure
 */
struct ringbuffer_sink_data {
	struct ringbuffer_entry *entries;
	uint32_t max_entries;
	uint32_t head;           /* Write position */
	uint32_t tail;           /* Read position */
	uint32_t count;          /* Current number of entries */
	rte_spinlock_t lock;     /* Thread safety */
	struct rte_sampler_sink *sink;  /* Back pointer for API functions */
};

/* Global registry for finding sink data from sink pointer */
#define MAX_RINGBUFFER_SINKS 16
static struct {
struct rte_sampler_sink *sink;
struct ringbuffer_sink_data *data;
} ringbuffer_registry[MAX_RINGBUFFER_SINKS];
static rte_spinlock_t registry_lock = RTE_SPINLOCK_INITIALIZER;

/**
 * Register sink in global registry
 */
static int
		register_sink(struct rte_sampler_sink *sink, struct ringbuffer_sink_data *data)
{
unsigned int i;

rte_spinlock_lock(&registry_lock);
for (i = 0; i < MAX_RINGBUFFER_SINKS; i++) {
if (ringbuffer_registry[i].sink == NULL) {
ringbuffer_registry[i].sink = sink;
ringbuffer_registry[i].data = data;
rte_spinlock_unlock(&registry_lock);
return 0;
}
}
rte_spinlock_unlock(&registry_lock);
return -ENOSPC;
}

/**
 * Unregister sink from global registry
 */
static void
		unregister_sink(struct rte_sampler_sink *sink)
{
unsigned int i;

rte_spinlock_lock(&registry_lock);
for (i = 0; i < MAX_RINGBUFFER_SINKS; i++) {
if (ringbuffer_registry[i].sink == sink) {
ringbuffer_registry[i].sink = NULL;
ringbuffer_registry[i].data = NULL;
break;
}
}
rte_spinlock_unlock(&registry_lock);
}

/**
 * Find sink data from sink pointer
 */
static struct ringbuffer_sink_data *
		find_sink_data(struct rte_sampler_sink *sink)
{
struct ringbuffer_sink_data *data = NULL;
unsigned int i;

rte_spinlock_lock(&registry_lock);
for (i = 0; i < MAX_RINGBUFFER_SINKS; i++) {
if (ringbuffer_registry[i].sink == sink) {
data = ringbuffer_registry[i].data;
break;
}
}
rte_spinlock_unlock(&registry_lock);
return data;
}

/**
 * Ring buffer sink output callback
 */
static int
		ringbuffer_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct ringbuffer_sink_data *data = user_data;
struct ringbuffer_entry *entry;
uint32_t pos;

(void)xstats_names;  /* Not stored in ring buffer */

if (data == NULL)
return -EINVAL;

rte_spinlock_lock(&data->lock);

/* Get write position */
pos = data->head;
entry = &data->entries[pos];

/* Free old entry if overwriting */
if (entry->valid) {
rte_free(entry->ids);
rte_free(entry->values);
}

/* Allocate and copy data */
		entry->ids = rte_malloc(NULL, sizeof(uint64_t) * n, 0);
		entry->values = rte_malloc(NULL, sizeof(uint64_t) * n, 0);

if (entry->ids == NULL || entry->values == NULL) {
rte_free(entry->ids);
rte_free(entry->values);
rte_spinlock_unlock(&data->lock);
return -ENOMEM;
}

/* Fill entry */
entry->timestamp = rte_get_timer_cycles();
rte_strscpy(entry->source_name, source_name, sizeof(entry->source_name));
entry->source_id = source_id;
entry->num_stats = n;
		memcpy(entry->ids, ids, sizeof(uint64_t) * n);
		memcpy(entry->values, values, sizeof(uint64_t) * n);
entry->valid = 1;

/* Advance head */
data->head = (data->head + 1) % data->max_entries;

/* Update count */
if (data->count < data->max_entries) {
data->count++;
} else {
/* Buffer full, advance tail (overwrite oldest) */
data->tail = (data->tail + 1) % data->max_entries;
}

rte_spinlock_unlock(&data->lock);

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ringbuffer_create)
struct rte_sampler_sink *
		rte_sampler_sink_ringbuffer_create(struct rte_sampler_session *session,
		const char *name,
		const struct rte_sampler_sink_ringbuffer_conf *conf)
{
struct rte_sampler_sink_ops ops;
struct ringbuffer_sink_data *data;
struct rte_sampler_sink *sink;

if (session == NULL || name == NULL || conf == NULL ||
    conf->max_entries == 0)
return NULL;

/* Allocate sink data */
data = rte_zmalloc(NULL, sizeof(*data), 0);
if (data == NULL)
return NULL;

/* Allocate ring buffer entries */
data->entries = rte_zmalloc(NULL,
    sizeof(struct ringbuffer_entry) * conf->max_entries,
    0);
if (data->entries == NULL) {
rte_free(data);
return NULL;
}

data->max_entries = conf->max_entries;
data->head = 0;
data->tail = 0;
data->count = 0;
rte_spinlock_init(&data->lock);

/* Setup sink operations */
ops.output = ringbuffer_sink_output;
ops.flags = RTE_SAMPLER_SINK_F_NO_NAMES;  /* Don't need names in ring buffer */

/* Register sink */
sink = rte_sampler_sink_register(session, name, &ops, data);
if (sink == NULL) {
rte_free(data->entries);
rte_free(data);
return NULL;
}

data->sink = sink;

/* Register in global registry */
if (register_sink(sink, data) < 0) {
rte_sampler_sink_unregister(sink);
rte_free(data->entries);
rte_free(data);
return NULL;
}

return sink;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ringbuffer_count)
int
		rte_sampler_sink_ringbuffer_count(struct rte_sampler_sink *sink)
{
struct ringbuffer_sink_data *data;
int count;

data = find_sink_data(sink);
if (data == NULL)
return -EINVAL;

rte_spinlock_lock(&data->lock);
count = data->count;
rte_spinlock_unlock(&data->lock);

return count;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ringbuffer_read)
int
		rte_sampler_sink_ringbuffer_read(struct rte_sampler_sink *sink,
		struct rte_sampler_ringbuffer_entry *entries,
		uint32_t max_entries)
{
struct ringbuffer_sink_data *data;
uint32_t i, pos, num_read;

data = find_sink_data(sink);
if (data == NULL || entries == NULL)
return -EINVAL;

rte_spinlock_lock(&data->lock);

num_read = (max_entries < data->count) ? max_entries : data->count;

for (i = 0; i < num_read; i++) {
pos = (data->tail + i) % data->max_entries;
struct ringbuffer_entry *src = &data->entries[pos];

if (!src->valid)
continue;

entries[i].timestamp = src->timestamp;
rte_strscpy(entries[i].source_name, src->source_name,
   sizeof(entries[i].source_name));
entries[i].source_id = src->source_id;
entries[i].num_stats = src->num_stats;

/* Caller needs to allocate arrays */
		entries[i].ids = rte_malloc(NULL, sizeof(uint64_t) * src->num_stats, 0);
		entries[i].values = rte_malloc(NULL, sizeof(uint64_t) * src->num_stats, 0);

if (entries[i].ids && entries[i].values) {
memcpy(entries[i].ids, src->ids,
		sizeof(uint64_t) * src->num_stats);
memcpy(entries[i].values, src->values,
		sizeof(uint64_t) * src->num_stats);
}
}

rte_spinlock_unlock(&data->lock);

return num_read;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ringbuffer_clear)
int
		rte_sampler_sink_ringbuffer_clear(struct rte_sampler_sink *sink)
{
struct ringbuffer_sink_data *data;
uint32_t i;

data = find_sink_data(sink);
if (data == NULL)
return -EINVAL;

rte_spinlock_lock(&data->lock);

/* Free all allocated data */
for (i = 0; i < data->max_entries; i++) {
if (data->entries[i].valid) {
rte_free(data->entries[i].ids);
rte_free(data->entries[i].values);
data->entries[i].valid = 0;
}
}

data->head = 0;
data->tail = 0;
data->count = 0;

rte_spinlock_unlock(&data->lock);

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ringbuffer_destroy)
int
		rte_sampler_sink_ringbuffer_destroy(struct rte_sampler_sink *sink)
{
struct ringbuffer_sink_data *data;
uint32_t i;

data = find_sink_data(sink);
if (data == NULL)
return -EINVAL;

/* Unregister from global registry */
unregister_sink(sink);

/* Clear and free all entries */
for (i = 0; i < data->max_entries; i++) {
if (data->entries[i].valid) {
rte_free(data->entries[i].ids);
rte_free(data->entries[i].values);
}
}

rte_free(data->entries);
rte_free(data);

rte_sampler_sink_unregister(sink);

return 0;
}
