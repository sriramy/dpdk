#ifndef RTE_SAMPLER_SINK_H
#define RTE_SAMPLER_SINK_H

#ifdef __cplusplus
extern "C" {
#endif


typedef bool (*rte_sampler_sink_start)(struct rte_sampler_sink *sink,
	struct rte_sampler_session *session);

typedef bool (*rte_sampler_sink_report_begin)(struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);

typedef int (*rte_sampler_sink_report_append)(struct rte_sampler_sink *sink,
    struct rte_sampler_session *session, const struct rte_sampler_stats_name names[],
    uint64_t values[], size_t capacity);

typedef bool (*rte_sampler_sink_report_end)(struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);

typedef bool (*rte_sampler_sink_stop)(struct rte_sampler_sink *sink,
    struct rte_sampler_session *session);

struct rte_sampler_sink_ops {
    rte_sampler_sink_start start;
    rte_sampler_sink_report_begin report_begin;
    rte_sampler_sink_report_append report_append;
    rte_sampler_sink_report_end report_end;
    rte_sampler_sink_stop stop;
};


#ifdef __cplusplus
}
#endif

#endif /* RTE_SAMPLER_SINK_H */