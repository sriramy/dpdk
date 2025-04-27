/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <rte_eventdev.h>
#include <rte_malloc.h>
#include "rte_sampler.h"

/*
 * Context structure for an event device sampler source.
 * This holds the event device identifier, the xstats mode (DEVICE, PORT, or QUEUE),
 * and an object ID (used for PORT/QUEUE modes).
 */
struct rte_event_dev_sampler_context {
    uint8_t dev_id;                           /* DPDK event device identifier */
    enum rte_event_dev_xstats_mode mode;      /* Xstats mode: DEVICE, PORT, or QUEUE */
    uint32_t obj_id;                          /* Object ID—for PORT/QUEUE modes */
};

/*
 * Callback to retrieve the available xstats entries (id → name mapping) from the event device.
 *
 * This function first queries the event device for the total number of available xstats entries.
 * It then allocates heap memory (using rte_malloc()) to retrieve the complete list, copies up to
 * max_xstats entries into the caller's array, and finally frees the temporary array.
 *
 * @param source_context Pointer to a rte_event_dev_sampler_context.
 * @param xstats         Array to be filled with xstats entries.
 * @param max_xstats     Maximum number of entries to copy.
 * @return The number of entries copied on success, or a negative error code.
 */
int rte_event_dev_sampler_get_xstats(void *source_context,
                                     struct rte_sampler_xstats_entry *xstats,
                                     uint16_t max_xstats)
{
    struct rte_event_dev_sampler_context *ctx = (struct rte_event_dev_sampler_context *)source_context;
    uint8_t dev_id = ctx->dev_id;
    uint16_t total = rte_event_dev_xstats_names_get(dev_id, ctx->mode, ctx->obj_id, NULL, 0);
    if (total == 0)
        return 0;

    /* Allocate heap memory for the complete xstats names list */
    struct rte_event_dev_xstats_name *dev_names = rte_malloc("rte_event_dev_xstats",
                                                               total * sizeof(struct rte_event_dev_xstats_name),
                                                               0);
    if (!dev_names)
        return -ENOMEM;

    int ret = rte_event_dev_xstats_names_get(dev_id, ctx->mode, ctx->obj_id, dev_names, total);
    if (ret < 0) {
        rte_free(dev_names);
        return ret;
    }

    uint16_t count = (ret < max_xstats) ? ret : max_xstats;
    for (uint16_t i = 0; i < count; i++) {
        xstats[i].id = dev_names[i].id;
        xstats[i].name = dev_names[i].name;
    }

    rte_free(dev_names);
    return count;
}

/*
 * Callback to fetch xstats values corresponding to a list of xstats IDs.
 *
 * This function queries the event device for the total number of available xstats values,
 * then allocates heap memory to retrieve all values. For each requested xstats ID in the ids
 * array, it copies the corresponding value into the output values array. Heap memory is then freed.
 *
 * @param source_context Pointer to a rte_event_dev_sampler_context.
 * @param ids            Array of xstats IDs to fetch.
 * @param values         Array where the fetched xstats values will be stored.
 * @param num_ids        Number of xstats IDs in the ids array.
 * @return 0 on success, or a negative error code.
 */
int rte_event_dev_sampler_fetch_xstats_values(void *source_context,
                                              const uint32_t *ids,
                                              uint64_t *values,
                                              uint16_t num_ids)
{
    struct rte_event_dev_sampler_context *ctx = (struct rte_event_dev_sampler_context *)source_context;
    uint8_t dev_id = ctx->dev_id;
    uint16_t total = rte_event_dev_xstats_get(dev_id, ctx->mode, ctx->obj_id, NULL, NULL, 0);
    if (total == 0)
        return 0;

    uint64_t *all_values = rte_malloc("rte_event_dev_xstats_values",
                                      total * sizeof(uint64_t), 0);
    if (!all_values)
        return -ENOMEM;

    int ret = rte_event_dev_xstats_get(dev_id, ctx->mode, ctx->obj_id, NULL, all_values, total);
    if (ret < 0) {
        rte_free(all_values);
        return ret;
    }

    for (uint16_t i = 0; i < num_ids; i++) {
        if (ids[i] < total)
            values[i] = all_values[ids[i]];
        else
            values[i] = 0;  /* Alternatively, you could return an error for an out-of-range ID */
    }

    rte_free(all_values);
    return 0;
}
