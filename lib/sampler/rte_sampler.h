/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _RTE_SAMPLER_H_
#define _RTE_SAMPLER_H_

/**
 * @file
 * RTE Sampler Library
 *
 * Generic sampler library that supports registering multiple sampler sources
 * (e.g., eventdev, ethdev, cryptodev) and multiple sampler sinks
 * (e.g., metrics, telemetry, file). Provides standard xstats-style API.
 *
 * Supports multiple sessions with independent sampling intervals and durations.
 *
 * Implementation Limits:
 * - Maximum sessions: 32
 * - Maximum sources per session: 64
 * - Maximum sinks per session: 16
 * - Maximum xstats per source: 256
 */

#include <stdint.h>
#include <rte_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum length of a sampler xstats name
 * Sized to accommodate composite names with source prefixes
 */
#define RTE_SAMPLER_XSTATS_NAME_SIZE 128

/**
 * Sink flags
 */
#define RTE_SAMPLER_SINK_F_NO_NAMES  0x0001
/**< Don't pass stat names to sink (optimization to avoid large data transfer) */

/**
 * Sampler xstats name structure
 */
struct rte_sampler_xstats_name {
char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
};

/**
 * Sampler source structure
 * Represents a source of statistics to be sampled
 */
struct rte_sampler_source;

/**
 * Sampler sink structure
 * Represents a destination for sampled statistics
 */
struct rte_sampler_sink;

/**
 * Sampler session structure
 * This data structure is intentionally opaque
 */
struct rte_sampler_session;

/**
 * Session configuration
 */
struct rte_sampler_session_conf {
uint64_t sample_interval_ms;  /**< Sampling interval in milliseconds (0 = manual) */
uint64_t duration_ms;         /**< Session duration in milliseconds (0 = infinite) */
const char *name;             /**< Optional session name for identification */
};

/**
 * Callback function type for source xstats_names_get
 *
 * @param source_id
 *   Source-specific identifier (e.g., device id)
 * @param xstats_names
 *   Array to be filled with stat names. Can be NULL to query size.
 * @param ids
 *   Array to be filled with stat IDs. Can be NULL to query size.
 * @param size
 *   Size of the arrays
 * @param user_data
 *   User-provided data passed during source registration
 * @return
 *   Number of stats available or negative on error
 */
typedef int (*rte_sampler_source_xstats_names_get_t)(
uint16_t source_id,
struct rte_sampler_xstats_name *xstats_names,
uint64_t *ids,
unsigned int size,
void *user_data);

/**
 * Callback function type for source xstats_get
 *
 * @param source_id
 *   Source-specific identifier (e.g., device id)
 * @param ids
 *   Array of stat IDs to retrieve
 * @param values
 *   Array to be filled with stat values
 * @param n
 *   Number of stats to retrieve
 * @param user_data
 *   User-provided data passed during source registration
 * @return
 *   Number of stats retrieved or negative on error
 */
typedef int (*rte_sampler_source_xstats_get_t)(
uint16_t source_id,
const uint64_t *ids,
uint64_t *values,
unsigned int n,
void *user_data);

/**
 * Callback function type for source xstats_reset
 *
 * @param source_id
 *   Source-specific identifier (e.g., device id)
 * @param ids
 *   Array of stat IDs to reset. NULL means reset all.
 * @param n
 *   Number of stats to reset
 * @param user_data
 *   User-provided data passed during source registration
 * @return
 *   Zero on success or negative on error
 */
typedef int (*rte_sampler_source_xstats_reset_t)(
uint16_t source_id,
const uint64_t *ids,
unsigned int n,
void *user_data);

/**
 * Sampler source operations structure
 */
struct rte_sampler_source_ops {
rte_sampler_source_xstats_names_get_t xstats_names_get;
rte_sampler_source_xstats_get_t xstats_get;
rte_sampler_source_xstats_reset_t xstats_reset;
};

/**
 * Callback function type for sink output
 *
 * @param source_name
 *   Name of the source
 * @param source_id
 *   Source identifier
 * @param xstats_names
 *   Array of stat names. Can be NULL if sink registered with RTE_SAMPLER_SINK_F_NO_NAMES flag.
 *   Sinks should check for NULL and use rte_sampler_source_get_xstats_name() if names are needed.
 * @param ids
 *   Array of stat IDs (always provided)
 * @param values
 *   Array of stat values (always provided)
 * @param n
 *   Number of stats
 * @param user_data
 *   User-provided data passed during sink registration
 * @return
 *   Zero on success or negative on error
 */
typedef int (*rte_sampler_sink_output_t)(
const char *source_name,
uint16_t source_id,
const struct rte_sampler_xstats_name *xstats_names,
const uint64_t *ids,
const uint64_t *values,
unsigned int n,
void *user_data);

/**
 * Sampler sink operations structure
 */
struct rte_sampler_sink_ops {
rte_sampler_sink_output_t output;
uint32_t flags;  /**< Sink flags (RTE_SAMPLER_SINK_F_*) */
};

/**
 * Free session structure
 *
 * @param session
 *   Pointer allocated by rte_sampler_session_create()
 *   If session is NULL, no operation is performed.
 */
void rte_sampler_session_free(struct rte_sampler_session *session);

/**
 * Allocate a sampler session
 *
 * @param conf
 *   Pointer to session configuration. If NULL, uses default config
 *   (manual sampling, infinite duration).
 * @return
 *   - Pointer to session structure on success
 *   - NULL on error (zmalloc failure)
 */
struct rte_sampler_session *rte_sampler_session_create(
const struct rte_sampler_session_conf *conf)
__rte_malloc __rte_dealloc(rte_sampler_session_free, 1);

/**
 * Start a sampling session
 *
 * For sessions with non-zero sample_interval_ms, starts automatic periodic sampling.
 * For manual sessions (sample_interval_ms = 0), just marks the session as active.
 *
 * @param session
 *   Pointer to session structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_session_start(struct rte_sampler_session *session);

/**
 * Stop a sampling session
 *
 * Stops automatic sampling and marks the session as inactive.
 *
 * @param session
 *   Pointer to session structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_session_stop(struct rte_sampler_session *session);

/**
 * Check if session is still active
 *
 * @param session
 *   Pointer to session structure
 * @return
 *   1 if active, 0 if stopped or expired, negative on error
 */
int rte_sampler_session_is_active(struct rte_sampler_session *session);

/**
 * Free source structure
 *
 * @param source
 *   Pointer returned by rte_sampler_source_register()
 *   If source is NULL, no operation is performed.
 */
void rte_sampler_source_free(struct rte_sampler_source *source);

/**
 * Register a sampler source to a session
 *
 * @param session
 *   Pointer to session structure
 * @param source_name
 *   Name of the source (e.g., "eventdev", "ethdev")
 * @param source_id
 *   Source-specific identifier (e.g., device id)
 * @param ops
 *   Pointer to source operations structure
 * @param user_data
 *   User-provided data to pass to callbacks
 * @return
 *   Pointer to source structure on success, NULL on error
 */
struct rte_sampler_source *rte_sampler_source_register(
struct rte_sampler_session *session,
const char *source_name,
uint16_t source_id,
const struct rte_sampler_source_ops *ops,
void *user_data)
__rte_malloc __rte_dealloc(rte_sampler_source_free, 1);

/**
 * Unregister a sampler source from a session
 *
 * @param source
 *   Pointer returned by rte_sampler_source_register()
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_source_unregister(struct rte_sampler_source *source);

/**
 * Free sink structure
 *
 * @param sink
 *   Pointer returned by rte_sampler_sink_register()
 *   If sink is NULL, no operation is performed.
 */
void rte_sampler_sink_free(struct rte_sampler_sink *sink);

/**
 * Register a sampler sink to a session
 *
 * @param session
 *   Pointer to session structure
 * @param sink_name
 *   Name of the sink (e.g., "metrics", "telemetry")
 * @param ops
 *   Pointer to sink operations structure
 * @param user_data
 *   User-provided data to pass to callbacks
 * @return
 *   Pointer to sink structure on success, NULL on error
 */
struct rte_sampler_sink *rte_sampler_sink_register(
struct rte_sampler_session *session,
const char *sink_name,
const struct rte_sampler_sink_ops *ops,
void *user_data)
__rte_malloc __rte_dealloc(rte_sampler_sink_free, 1);

/**
 * Unregister a sampler sink from a session
 *
 * @param sink
 *   Pointer returned by rte_sampler_sink_register()
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_unregister(struct rte_sampler_sink *sink);

/**
 * Sample statistics from all registered sources and send to all registered sinks
 *
 * This function can be called manually for any session, or will be called
 * automatically for sessions with non-zero sample_interval_ms after calling
 * rte_sampler_session_start().
 *
 * @param session
 *   Pointer to session structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sample(struct rte_sampler_session *session);

/**
 * Poll all active sessions for automatic sampling
 *
 * This function should be called periodically from the main loop to trigger
 * automatic sampling for sessions that have sample_interval_ms > 0.
 * It's a no-op for manual sessions.
 *
 * @return
 *   Number of sessions polled, or negative on error
 */
int rte_sampler_poll(void);

/**
 * Get xstats names from session
 *
 * @param session
 *   Pointer to session structure
 * @param source
 *   Pointer to source (use NULL for all sources)
 * @param xstats_names
 *   Array to be filled with stat names. Can be NULL to query size.
 * @param size
 *   Size of the array
 * @return
 *   Number of stats available or negative on error
 */
int rte_sampler_xstats_names_get(struct rte_sampler_session *session,
  struct rte_sampler_source *source,
  struct rte_sampler_xstats_name *xstats_names,
  unsigned int size);

/**
 * Get xstats values from session
 *
 * @param session
 *   Pointer to session structure
 * @param source
 *   Pointer to source (use NULL for all sources)
 * @param ids
 *   Array of stat IDs to retrieve
 * @param values
 *   Array to be filled with stat values
 * @param n
 *   Number of stats to retrieve
 * @return
 *   Number of stats retrieved or negative on error
 */
int rte_sampler_xstats_get(struct rte_sampler_session *session,
    struct rte_sampler_source *source,
    const uint64_t *ids,
    uint64_t *values,
    unsigned int n);

/**
 * Reset xstats in session
 *
 * @param session
 *   Pointer to session structure
 * @param source
 *   Pointer to source (use NULL for all sources)
 * @param ids
 *   Array of stat IDs to reset. NULL means reset all.
 * @param n
 *   Number of stats to reset
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_xstats_reset(struct rte_sampler_session *session,
      struct rte_sampler_source *source,
      const uint64_t *ids,
      unsigned int n);

/**
 * Get xstats name for a specific source
 *
 * Helper function for sinks that use RTE_SAMPLER_SINK_F_NO_NAMES flag.
 * Allows on-demand lookup of stat names by ID.
 *
 * @param source
 *   Pointer to source structure
 * @param id
 *   Stat ID to look up
 * @param name
 *   Pointer to buffer to store the name
 * @return
 *   Zero on success, negative on error (-ENOENT if ID not found)
 */
int rte_sampler_source_get_xstats_name(struct rte_sampler_source *source,
					uint64_t id,
					struct rte_sampler_xstats_name *name);

/**
 * Set xstats filter for a source by name patterns
 *
 * Allows filtering which statistics to sample based on name patterns.
 * Only matching stats will be sampled. Clear filter to sample all stats.
 *
 * @param source
 *   Pointer to source structure
 * @param patterns
 *   Array of name patterns to match (supports wildcards: * and ?)
 * @param num_patterns
 *   Number of patterns in array
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_source_set_filter(struct rte_sampler_source *source,
				   const char **patterns,
				   unsigned int num_patterns);

/**
 * Clear xstats filter for a source
 *
 * Removes any active filter, allowing all stats to be sampled.
 *
 * @param source
 *   Pointer to source structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_source_clear_filter(struct rte_sampler_source *source);

/**
 * Get active filter patterns for a source
 *
 * @param source
 *   Pointer to source structure
 * @param patterns
 *   Array to store filter patterns
 * @param max_patterns
 *   Maximum number of patterns to retrieve
 * @return
 *   Number of active patterns, or negative on error
 */
int rte_sampler_source_get_filter(struct rte_sampler_source *source,
				   char **patterns,
				   unsigned int max_patterns);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_H_ */
