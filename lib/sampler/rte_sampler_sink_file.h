/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _RTE_SAMPLER_SINK_FILE_H_
#define _RTE_SAMPLER_SINK_FILE_H_

/**
 * @file
 * RTE Sampler File Sink
 *
 * File sink implementation for the sampler library.
 * Writes sampled statistics to a file in various formats.
 */

#include <stdint.h>
#include <stdio.h>
#include <rte_sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * File sink output format
 */
enum rte_sampler_sink_file_format {
RTE_SAMPLER_SINK_FILE_FORMAT_CSV = 0,   /**< CSV format */
RTE_SAMPLER_SINK_FILE_FORMAT_JSON,      /**< JSON format */
RTE_SAMPLER_SINK_FILE_FORMAT_TEXT,      /**< Plain text format */
};

/**
 * File sink configuration
 */
struct rte_sampler_sink_file_conf {
	const char *filepath;                           /**< Output file path */
	enum rte_sampler_sink_file_format format;       /**< Output format */
	uint32_t buffer_size;                           /**< I/O buffer size (0=default) */
	uint8_t append;                                 /**< 1=append, 0=overwrite */
};

/**
 * Create and register a file sink
 *
 * @param session
 *   Pointer to sampler session structure
 * @param name
 *   Name for this sink instance
 * @param conf
 *   Pointer to file sink configuration
 * @return
 *   Pointer to sink structure on success, NULL on error
 */
struct rte_sampler_sink *rte_sampler_sink_file_create(
struct rte_sampler_session *session,
const char *name,
const struct rte_sampler_sink_file_conf *conf);

/**
 * Destroy a file sink
 *
 * @param sink
 *   Pointer to sink structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_file_destroy(struct rte_sampler_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_SINK_FILE_H_ */
