/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _RTE_SAMPLER_SINK_RINGBUFFER_H_
#define _RTE_SAMPLER_SINK_RINGBUFFER_H_

/**
 * @file
 * RTE Sampler Ring Buffer Sink
 *
 * Ring buffer sink implementation for the sampler library.
 * Stores sampled statistics in a fixed-size circular buffer.
 */

#include <stdint.h>
#include <rte_sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ring buffer sample entry
 */
struct rte_sampler_ringbuffer_entry {
	uint64_t timestamp;                  /**< Sample timestamp (cycles) */
	char source_name[64];                /**< Source name */
	uint16_t source_id;                  /**< Source ID */
	uint16_t num_stats;                  /**< Number of stats in this entry */
	uint64_t *ids;                       /**< Stat IDs */
	uint64_t *values;                    /**< Stat values */
};

/**
 * Ring buffer sink configuration
 */
struct rte_sampler_sink_ringbuffer_conf {
	uint32_t max_entries;  /**< Maximum number of entries in ring buffer */
};

/**
 * Create and register a ring buffer sink
 *
 * @param session
 *   Pointer to sampler session structure
 * @param name
 *   Name for this sink instance
 * @param conf
 *   Pointer to ring buffer configuration
 * @return
 *   Pointer to sink structure on success, NULL on error
 */
struct rte_sampler_sink *rte_sampler_sink_ringbuffer_create(
struct rte_sampler_session *session,
const char *name,
const struct rte_sampler_sink_ringbuffer_conf *conf);

/**
 * Get number of entries currently in ring buffer
 *
 * @param sink
 *   Pointer to sink structure
 * @return
 *   Number of entries, or negative on error
 */
int rte_sampler_sink_ringbuffer_count(struct rte_sampler_sink *sink);

/**
 * Read entries from ring buffer
 *
 * @param sink
 *   Pointer to sink structure
 * @param entries
 *   Array to store retrieved entries
 * @param max_entries
 *   Maximum number of entries to retrieve
 * @return
 *   Number of entries retrieved, or negative on error
 */
int rte_sampler_sink_ringbuffer_read(struct rte_sampler_sink *sink,
		struct rte_sampler_ringbuffer_entry *entries,
		uint32_t max_entries);

/**
 * Clear all entries from ring buffer
 *
 * @param sink
 *   Pointer to sink structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_ringbuffer_clear(struct rte_sampler_sink *sink);

/**
 * Destroy a ring buffer sink
 *
 * @param sink
 *   Pointer to sink structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_ringbuffer_destroy(struct rte_sampler_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_SINK_RINGBUFFER_H_ */
