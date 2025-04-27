/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rte_malloc.h>
#include "rte_sampler.h"

/* Structure for the file sink context. It holds a pointer to an open file. */
struct file_sink_context {
    FILE *fp;
};

/**
 * @brief Callback to process collected xstats.
 *
 * Writes each xstats sample (ID and value) to the file held in the sink context.
 *
 * @param sink_context User-defined context (a pointer to struct file_sink_context).
 * @param ids Array of xstats IDs.
 * @param values Array of corresponding xstats values.
 * @param num_ids Number of xstats entries.
 * @return 0 on success, negative error code on failure.
 */
int file_sink_process_xstats(void *sink_context,
                             const uint32_t *ids,
                             const uint64_t *values,
                             uint16_t num_ids)
{
    struct file_sink_context *ctx = (struct file_sink_context *)sink_context;
    if (!ctx || !ctx->fp)
        return -EINVAL;

    for (uint16_t i = 0; i < num_ids; i++) {
        fprintf(ctx->fp, "xstats id: %u, value: %llu\n",
                ids[i], (unsigned long long) values[i]);
    }
    fflush(ctx->fp);
    return 0;
}

/**
 * @brief Create a file-based sink.
 *
 * Allocates and initializes a new sink that writes output to the specified file.
 *
 * @param filepath The path of the file to write the xstats output.
 * @param buffer_size Buffer size for the sink (used by other parts of the library).
 * @return Pointer to a newly created sink on success, or NULL on failure.
 */
struct rte_sampler_sink *create_file_sink(const char *filepath, uint32_t buffer_size)
{
    if (!filepath)
        return NULL;

    /* Allocate and initialize the file sink context */
    struct file_sink_context *ctx = rte_malloc("file_sink_context",
                                               sizeof(struct file_sink_context),
                                               0);
    if (!ctx)
        return NULL;

    ctx->fp = fopen(filepath, "w");
    if (!ctx->fp) {
        rte_free(ctx);
        return NULL;
    }

    /* Allocate the sink structure */
    struct rte_sampler_sink *sink = rte_malloc("rte_sampler_sink",
                                                 sizeof(struct rte_sampler_sink),
                                                 0);
    if (!sink) {
        fclose(ctx->fp);
        rte_free(ctx);
        return NULL;
    }
    sink->buffer_size = buffer_size;
    sink->process_xstats = file_sink_process_xstats;
    sink->sink_context = ctx;
    return sink;
}

/**
 * @brief Destroy a file-based sink.
 *
 * Closes any open file and frees the memory associated with the sink and its context.
 *
 * @param sink Pointer to the sink to destroy.
 */
void destroy_file_sink(struct rte_sampler_sink *sink)
{
    if (!sink)
        return;

    struct file_sink_context *ctx = (struct file_sink_context *)sink->sink_context;
    if (ctx) {
        if (ctx->fp)
            fclose(ctx->fp);
        rte_free(ctx);
    }
    rte_free(sink);
}
