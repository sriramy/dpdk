#ifndef RTE_SAMPLER_H
#define RTE_SAMPLER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define SAMPLER_MAX_NAME_LEN 64

struct rte_sampler_stats_name {
	char name[SAMPLER_MAX_NAME_LEN];
};

struct rte_sampler_session;
struct rte_sampler_source;
struct rte_sampler_source_ops;
struct rte_sampler_sink;
struct rte_sampler_sink_ops;

__rte_experimental
struct rte_sampler_session *
rte_sampler_session_create(uint64_t interval);

__rte_experimental
void
rte_sampler_session_free(struct rte_sampler_session *session);

__rte_experimental
struct rte_sampler_source *
rte_sampler_session_register_source(struct rte_sampler_session *session,
    const char *source_name, struct rte_sampler_source_ops ops);

__rte_experimental
int
rte_sampler_session_unregister_source(struct rte_sampler_session *session,
    struct rte_sampler_source *source);

__rte_experimental
struct rte_sampler_sink *
rte_sampler_session_register_sink(struct rte_sampler_session *session,
    const char *sink_name, struct rte_sampler_sink_ops ops);

__rte_experimental
int
rte_sampler_session_unregister_sink(struct rte_sampler_session *session,
    struct rte_sampler_sink *sink);

__rte_experimental
int
rte_sampler_session_register_stats(struct rte_sampler_session *session,
    struct rte_sampler_source *source, struct rte_sampler_stats_name names[],
    const uint64_t ids[], size_t capacity);

__rte_experimental
int
rte_sampler_session_start(struct rte_sampler_session *session, uint64_t duration);

__rte_experimental
int
rte_sampler_session_stop(struct rte_sampler_session *session);

__rte_experimental
int
rte_sampler_session_process(struct rte_sampler_session *session);

#ifdef __cplusplus
}
#endif

#endif /* RTE_SAMPLER_H */