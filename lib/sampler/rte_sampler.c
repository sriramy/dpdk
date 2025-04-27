/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#include <rte_common.h>
#include <rte_errno.h>
#include <rte_log.h>
#include <rte_malloc.h>

#include <rte_sampler.h>
#include <rte_sampler_source.h>

int rte_sampler_source_register(struct rte_sampler_session *session,
    struct rte_sampler_source_ops *source_ops,
    struct rte_sampler_source *source)
{
    if (session == NULL || source_ops == NULL || source == NULL) {
        rte_errno = EINVAL;
        return -rte_errno;
    }

    if (session->source != NULL) {
        rte_errno = EBUSY;
        return -rte_errno;
    }

    session->source = source;
    session->source_ops = source_ops;

    return 0;
}

int rte_sampler_source_unregister(struct rte_sampler_session *session,
    struct rte_sampler_source *source)
{
    if (session == NULL || source == NULL) {
        rte_errno = EINVAL;
        return -rte_errno;
    }

    if (session->source != source) {
        rte_errno = EBUSY;
        return -rte_errno;
    }

    session->source = NULL;
    session->source_ops = NULL;

    return 0;
}
int rte_sampler_source_register_xstats(struct rte_sampler_session *session,
    struct rte_sampler_xstats_name *xstats_names,
    uint64_t *ids,
    uint16_t capacity)
{
    #define MAX_XSTATS_IDS 1024
    static uint64_t id_bitmap[MAX_XSTATS_IDS / 64] = {0};
    static uint64_t next_free_id = 0;
        rte_errno = EBUSY;
        return -rte_errno;
    }

    session->xstats_names = xstats_names;
    session->xstats_ids = ids;
    session->xstats_capacity = capacity;

    return 0;
}