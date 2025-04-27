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

struct rte_sampler_source {
    int (*get_xstats)(void *source_context,
                      struct rte_sampler_xstats_entry *xstats,
                      uint16_t max_xstats);

    int (*fetch_xstats_values)(void *source_context,
                               const uint32_t *ids,
                               uint64_t *values,
                               uint16_t num_ids);

    void *ctx;
};

struct rte_sampler_session;

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_H_ */
