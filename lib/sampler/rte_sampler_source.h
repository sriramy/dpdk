#ifndef RTE_SAMPLER_SOURCE_H
#define RTE_SAMPLER_SOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

struct rte_sampler_source;

typedef bool (*rte_sampler_source_start)(struct rte_sampler_source *source,
	struct rte_sampler_session *session);

typedef int (*rte_sampler_source_collect)(struct rte_sampler_source *source,
    struct rte_sampler_session *session, const uint64_t ids[], uint64_t values[],
    size_t capacity);

typedef bool (*rte_sampler_source_stop)(struct rte_sampler_source *source,
    struct rte_sampler_session *session);

struct rte_sampler_source_ops {
    rte_sampler_source_start start;
    rte_sampler_source_collect collect;
    rte_sampler_source_stop stop;
};

#ifdef __cplusplus
}
#endif

#endif /* RTE_SAMPLER_SOURCE_H */