/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#ifndef _RTE_SAMPLER_SOURCE_H_
#define _RTE_SAMPLER_SOURCE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <rte_common.h>
#include <errno.h>

#include <rte_sampler.h>

#define RTE_SAMPLER_XSTATS_NAME_SIZE 64

struct rte_sampler_xstats_name {
    char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
};

struct rte_sampler_source;

typedef int (*sampler_source_start_t)(struct rte_sampler_session *session);
typedef int (*sampler_source_collect_t)(struct rte_sampler_session *session,
                                        const uint32_t *ids,
                                        const uint64_t *values,
                                        uint16_t capacity);
typedef int (*sampler_source_stop_t)(struct rte_sampler_session *session);

struct rte_sampler_source_ops {
    sampler_source_start_t start;
    sampler_source_collect_t collect;
    sampler_source_stop_t stop;
};

int
rte_sampler_source_register(struct rte_sampler_session *session,
    struct rte_sampler_source_ops *source_ops,
    struct rte_sampler_source *source);

int
rte_sampler_source_register_xstats(struct rte_sampler_session *session,
    struct rte_sampler_xstats_name *xstats_names,
    uint64_t *ids,
    uint16_t capacity);

int
rte_sampler_source_unregister(struct rte_sampler_session *session,
    struct rte_sampler_source *source);
    
#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_SOURCE_H_ */
