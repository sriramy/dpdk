/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#include <rte_common.h>
#include <rte_errno.h>
#include <rte_log.h>

/* Keep track of allocated IDs using a bitmap */
static uint64_t allocated_ids;
static uint64_t next_free_id;

uint64_t rte_sampler_xstats_get_next_free_id(void)
{
    uint64_t id = next_free_id;
    while (allocated_ids & (1ULL << id)) {
        id = (id + 1) % 64;
        if (id == next_free_id)
            return UINT64_MAX; /* No free IDs */
    }
    allocated_ids |= (1ULL << id);
    next_free_id = (id + 1) % 64;
    return id;
}

void rte_sampler_xstats_set_next_free_id(uint64_t id)
{
    next_free_id = id % 64;
}

void rte_sampler_xstats_set_id_bit(uint64_t id)
{
    allocated_ids |= (1ULL << (id % 64));
}

void rte_sampler_xstats_clear_id_bit(uint64_t id)
{
    allocated_ids &= ~(1ULL << (id % 64));
}

bool rte_sampler_xstats_is_id_used(uint64_t id)
{
    return (allocated_ids & (1ULL << (id % 64))) != 0;
}
