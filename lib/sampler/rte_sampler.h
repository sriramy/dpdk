/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#ifndef _RTE_SAMPLER_H_
#define _RTE_SAMPLER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_common.h>
#include <errno.h>

/**
 * @brief A single xstats entry provided by a source.
 *
 * Each xstats entry maps an ID to a human‐readable string name.
 */
struct rte_sampler_xstats_entry {
    uint32_t id;        /**< Unique identifier for this xstats entry */
    const char *name;   /**< Name for the xstats entry */
};

/**
 * @brief Structure representing the configuration for a sampler source.
 *
 * Each source advertises a list of available xstats entries (id/name pairs)
 * and is responsible for fetching a set of xstats values when requested.
 */
struct rte_sampler_source {
    uint32_t sample_rate; /**< Sampling rate in Hz */

    /**
     * @brief Retrieve the list of available xstats entries.
     *
     * The source fills up to max_xstats entries into the provided xstats array.
     *
     * @param source_context User-defined context pointer.
     * @param xstats Array to be populated with xstats entries.
     * @param max_xstats Maximum number of entries to write.
     * @return The number of available xstats entries on success,
     *         or a negative error code on failure.
     */
    int (*get_xstats)(void *source_context,
                      struct rte_sampler_xstats_entry *xstats,
                      uint16_t max_xstats);

    /**
     * @brief Fetch the xstats values for a given list of xstats IDs.
     *
     * The source writes up to num_ids values into the given values array.
     *
     * @param source_context User-defined context pointer.
     * @param ids Array of xstats IDs for which values are requested.
     * @param values Array where the fetched xstats values will be stored.
     * @param num_ids Number of IDs provided.
     * @return 0 on success, or a negative error code on failure.
     */
    int (*fetch_xstats_values)(void *source_context,
                               const uint32_t *ids,
                               uint64_t *values,
                               uint16_t num_ids);

    void *source_context; /**< Source-specific context pointer */
};

/**
 * @brief Structure representing the configuration for a sampler sink.
 *
 * The sink receives the set of collected xstats values from one or more sources.
 */
struct rte_sampler_sink {
    uint32_t buffer_size; /**< Buffer size, used for temporary storage of sampled data */

    /**
     * @brief Process the collected xstats.
     *
     * @param sink_context User-defined context pointer.
     * @param ids Array of xstats IDs that were sampled.
     * @param values Array of corresponding xstats values.
     * @param num_ids Number of xstats entries.
     * @return 0 on success, or a negative error code.
     */
    int (*process_xstats)(void *sink_context,
                          const uint32_t *ids,
                          const uint64_t *values,
                          uint16_t num_ids);

    void *sink_context; /**< Sink-specific context pointer */
};

/**
 * @brief Opaque structure representing a sampler session.
 *
 * A session groups the set of sources with one sink and handles the
 * filtering (by xstats names) and collection of xstats values.
 */
struct rte_sampler_session;

/**
 * @brief Create a new sampler session.
 *
 * @param sink Pointer to the sink configuration.
 * @return Pointer to a new session on success, or NULL on error.
 */
struct rte_sampler_session *rte_sampler_session_create(struct rte_sampler_sink *sink);

/**
 * @brief Add a sampler source to an existing session.
 *
 * @param session Pointer to an existing sampler session.
 * @param source Pointer to the sampler source to add.
 * @return 0 on success, or a negative error code.
 */
int rte_sampler_session_add_source(struct rte_sampler_session *session,
                                   struct rte_sampler_source *source);

/**
 * @brief Set the xstats filter for the session.
 *
 * The filter is an array of strings (stat names). These names are used to match
 * against the xstats entries advertised by each source.
 *
 * @param session Pointer to the sampler session.
 * @param names Array of filter strings.
 * @param count Number of filter strings.
 * @return 0 on success, or a negative error code.
 */
int rte_sampler_session_set_xstats_filter(struct rte_sampler_session *session,
                                          const char **names,
                                          uint16_t count);

/**
 * @brief Start the sampler session.
 *
 * This function performs a one-time filtering operation. For every source, it queries
 * the available xstats entries and maps the provided filter names to the corresponding
 * xstats IDs. The results (filtered IDs) are cached for use during sample collection.
 *
 * @param session Pointer to the sampler session.
 * @return 0 on success, or a negative error code.
 */
int rte_sampler_session_start(struct rte_sampler_session *session);

/**
 * @brief Process the sampler session.
 *
 * For each source, the session sends the cached list of filtered xstats IDs to the source
 * and collects its current xstats values. The sink is then called to process the data.
 *
 * @param session Pointer to the sampler session.
 * @return 0 on success, or a negative error code.
 */
int rte_sampler_session_process(struct rte_sampler_session *session);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_H_ */
