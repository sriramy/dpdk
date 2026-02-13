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
 */

#include <stdint.h>
#include <rte_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Maximum length of a sampler xstats name
 */
#define RTE_SAMPLER_XSTATS_NAME_SIZE 64

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
 * Sampler context structure
 * This data structure is intentionally opaque
 */
struct rte_sampler;

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
 *   Array of stat names
 * @param ids
 *   Array of stat IDs
 * @param values
 *   Array of stat values
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
};

/**
 * Free sampler structure
 *
 * @param sampler
 *   Pointer allocated by rte_sampler_create()
 *   If sampler is NULL, no operation is performed.
 */
void rte_sampler_free(struct rte_sampler *sampler);

/**
 * Allocate a sampler structure
 *
 * @return
 *   - Pointer to structure on success
 *   - NULL on error (zmalloc failure)
 */
struct rte_sampler *rte_sampler_create(void)
__rte_malloc __rte_dealloc(rte_sampler_free, 1);

/**
 * Register a sampler source
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param source_name
 *   Name of the source (e.g., "eventdev", "ethdev")
 * @param source_id
 *   Source-specific identifier (e.g., device id)
 * @param ops
 *   Pointer to source operations structure
 * @param user_data
 *   User-provided data to pass to callbacks
 * @return
 *   Source handle on success, negative on error
 */
int rte_sampler_source_register(struct rte_sampler *sampler,
 const char *source_name,
 uint16_t source_id,
 const struct rte_sampler_source_ops *ops,
 void *user_data);

/**
 * Unregister a sampler source
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param source_handle
 *   Source handle returned by rte_sampler_source_register()
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_source_unregister(struct rte_sampler *sampler,
   int source_handle);

/**
 * Register a sampler sink
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param sink_name
 *   Name of the sink (e.g., "metrics", "telemetry")
 * @param ops
 *   Pointer to sink operations structure
 * @param user_data
 *   User-provided data to pass to callbacks
 * @return
 *   Sink handle on success, negative on error
 */
int rte_sampler_sink_register(struct rte_sampler *sampler,
       const char *sink_name,
       const struct rte_sampler_sink_ops *ops,
       void *user_data);

/**
 * Unregister a sampler sink
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param sink_handle
 *   Sink handle returned by rte_sampler_sink_register()
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_unregister(struct rte_sampler *sampler,
 int sink_handle);

/**
 * Sample statistics from all registered sources and send to all registered sinks
 *
 * @param sampler
 *   Pointer to sampler structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sample(struct rte_sampler *sampler);

/**
 * Get xstats names from sampler
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param source_handle
 *   Source handle (use -1 for all sources)
 * @param xstats_names
 *   Array to be filled with stat names. Can be NULL to query size.
 * @param size
 *   Size of the array
 * @return
 *   Number of stats available or negative on error
 */
int rte_sampler_xstats_names_get(struct rte_sampler *sampler,
  int source_handle,
  struct rte_sampler_xstats_name *xstats_names,
  unsigned int size);

/**
 * Get xstats values from sampler
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param source_handle
 *   Source handle (use -1 for all sources)
 * @param ids
 *   Array of stat IDs to retrieve
 * @param values
 *   Array to be filled with stat values
 * @param n
 *   Number of stats to retrieve
 * @return
 *   Number of stats retrieved or negative on error
 */
int rte_sampler_xstats_get(struct rte_sampler *sampler,
    int source_handle,
    const uint64_t *ids,
    uint64_t *values,
    unsigned int n);

/**
 * Reset xstats in sampler
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param source_handle
 *   Source handle (use -1 for all sources)
 * @param ids
 *   Array of stat IDs to reset. NULL means reset all.
 * @param n
 *   Number of stats to reset
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_xstats_reset(struct rte_sampler *sampler,
      int source_handle,
      const uint64_t *ids,
      unsigned int n);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_H_ */
