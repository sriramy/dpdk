/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Ericsson AB
 */

#include "rte_sampler.h"
#include <rte_log.h>
#include <rte_malloc.h>
#include <string.h>

#define RTE_LOGTYPE_SAMPLER RTE_LOGTYPE_USER1

/* Define maximum numbers for our internal arrays */
#define MAX_SAMPLER_SESSION_SOURCES 16
#define MAX_XSTATS_FILTERS 32
#define MAX_SUPPORTED_XSTATS 64
#define MAX_FILTERED_XSTATS 32  /* Maximum filtered xstats IDs per source */

/* Internal structure to cache filtered xstats IDs for a single source */
struct rte_sampler_filtered_xstats {
    struct rte_sampler_source *source;         /* Associated source pointer */
    uint32_t filtered_ids[MAX_FILTERED_XSTATS];  /* Array of xstats IDs matching filter */
    uint16_t num_filtered_ids;                   /* Number of filtered IDs cached */
};

/* Internal definition of the sampler session */
struct rte_sampler_session {
    struct rte_sampler_sink *sink;
    struct rte_sampler_source *sources[MAX_SAMPLER_SESSION_SOURCES];
    uint16_t num_sources;

    /* Application-provided filter names (stat names) */
    const char *filter_names[MAX_XSTATS_FILTERS];
    uint16_t num_filter_names;

    /* Cached filtered xstats for each source (indexed by source index) */
    struct rte_sampler_filtered_xstats filtered_xstats[MAX_SAMPLER_SESSION_SOURCES];
};

struct rte_sampler_session *
rte_sampler_session_create(struct rte_sampler_sink *sink)
{
    struct rte_sampler_session *session = rte_malloc("rte_sampler_session",
                                                      sizeof(struct rte_sampler_session), 0);
    if (!session)
        return NULL;
    session->sink = sink;
    session->num_sources = 0;
    session->num_filter_names = 0;
    memset(session->filter_names, 0, sizeof(session->filter_names));
    for (uint16_t i = 0; i < MAX_SAMPLER_SESSION_SOURCES; i++) {
        session->filtered_xstats[i].source = NULL;
        session->filtered_xstats[i].num_filtered_ids = 0;
    }
    return session;
}

int rte_sampler_session_add_source(struct rte_sampler_session *session,
                                   struct rte_sampler_source *source)
{
    if (!session || !source)
        return -EINVAL;
    if (session->num_sources >= MAX_SAMPLER_SESSION_SOURCES)
        return -ENOMEM;
    session->sources[session->num_sources++] = source;
    return 0;
}

int rte_sampler_session_set_xstats_filter(struct rte_sampler_session *session,
                                          const char **names,
                                          uint16_t count)
{
    if (!session || !names || count == 0 || count > MAX_XSTATS_FILTERS)
        return -EINVAL;
    session->num_filter_names = count;
    memcpy(session->filter_names, names, count * sizeof(const char *));
    return 0;
}

int rte_sampler_session_start(struct rte_sampler_session *session)
{
    if (!session)
        return -EINVAL;

    for (uint16_t i = 0; i < session->num_sources; i++) {
        struct rte_sampler_source *source = session->sources[i];
        struct rte_sampler_filtered_xstats *filtered = &session->filtered_xstats[i];
        filtered->source = source;
        filtered->num_filtered_ids = 0;

        struct rte_sampler_xstats_entry xstats[MAX_SUPPORTED_XSTATS];
        int num_xstats = source->get_xstats(source->source_context, xstats, MAX_SUPPORTED_XSTATS);
        if (num_xstats < 0)
            return num_xstats;

        for (int j = 0; j < num_xstats; j++) {
            for (uint16_t k = 0; k < session->num_filter_names; k++) {
                if (strcmp(xstats[j].name, session->filter_names[k]) == 0) {
                    if (filtered->num_filtered_ids < MAX_FILTERED_XSTATS) {
                        filtered->filtered_ids[filtered->num_filtered_ids++] = xstats[j].id;
                    } else {
                        return -ENOMEM;
                    }
                    break;
                }
            }
        }
    }
    return 0;
}

int rte_sampler_session_process(struct rte_sampler_session *session)
{
    if (!session)
        return -EINVAL;

    uint64_t values[MAX_FILTERED_XSTATS];

    for (uint16_t i = 0; i < session->num_sources; i++) {
        struct rte_sampler_filtered_xstats *filtered = &session->filtered_xstats[i];
        if (filtered->num_filtered_ids == 0)
            continue;
        struct rte_sampler_source *source = filtered->source;
        int ret = source->fetch_xstats_values(source->source_context,
                                              filtered->filtered_ids,
                                              values,
                                              filtered->num_filtered_ids);
        if (ret < 0)
            return ret;
        ret = session->sink->process_xstats(session->sink->sink_context,
                                            filtered->filtered_ids,
                                            values,
                                            filtered->num_filtered_ids);
        if (ret < 0)
            return ret;
    }

    return 0;
}

int rte_sampler_session_destroy(struct rte_sampler_session *session)
{
    if (!session)
        return -EINVAL;

    /* Free any dynamically allocated resources if needed */
    for (uint16_t i = 0; i < session->num_sources; i++) {
        session->filtered_xstats[i].source = NULL;
        session->filtered_xstats[i].num_filtered_ids = 0;
    }

    /* Free the session itself */
    rte_free(session);

    return 0;
}
