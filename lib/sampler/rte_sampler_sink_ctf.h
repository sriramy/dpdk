/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _RTE_SAMPLER_SINK_CTF_H_
#define _RTE_SAMPLER_SINK_CTF_H_

/**
 * @file
 * RTE Sampler CTF Sink
 *
 * CTF (Common Trace Format) sink implementation for the sampler library.
 * Writes sampled statistics in CTF format compatible with trace viewers.
 */

#include <stdint.h>
#include <rte_sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CTF sink configuration
 */
struct rte_sampler_sink_ctf_conf {
const char *trace_dir;     /**< Output trace directory */
const char *trace_name;    /**< Trace name */
};

/**
 * Create and register a CTF sink
 *
 * @param session
 *   Pointer to sampler session structure
 * @param name
 *   Name for this sink instance
 * @param conf
 *   Pointer to CTF configuration
 * @return
 *   Pointer to sink structure on success, NULL on error
 */
struct rte_sampler_sink *rte_sampler_sink_ctf_create(
struct rte_sampler_session *session,
const char *name,
const struct rte_sampler_sink_ctf_conf *conf);

/**
 * Destroy a CTF sink
 *
 * @param sink
 *   Pointer to sink structure
 * @return
 *   Zero on success, negative on error
 */
int rte_sampler_sink_ctf_destroy(struct rte_sampler_sink *sink);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_SINK_CTF_H_ */
