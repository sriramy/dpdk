#include <sys/queue.h>

#include <eal_export.h>
#include <rte_debug.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_errno.h>
#include <rte_string_fns.h>

#include "rte_sampler.h"
#include "rte_sampler_source.h"
#include "rte_sampler_sink.h"

#define SAMPLER_MIN_INTERVAL 1 * 1000 // 1ms

RTE_LOG_REGISTER_SUFFIX(sampler_logtype, sampler, INFO);
#define RTE_LOGTYPE_SAMPLER	sampler_logtype

#define SAMPLER_LOG_ERR(...)	\
	RTE_LOG_LINE_PREFIX(ERR, SAMPLER, "%s() line %u: ", \
		__func__ RTE_LOG_COMMA __LINE__, __VA_ARGS__)

#define SAMPLER_LOG_INFO(...) \
	RTE_LOG_LINE_PREFIX(INFO, SAMPLER, "%s() line %u: ", \
		__func__ RTE_LOG_COMMA __LINE__, __VA_ARGS__)

#ifdef RTE_LIBRTE_SAMPLER_DEBUG
#define SAMPLER_LOG_DBG(...)	\
	RTE_LOG_LINE_PREFIX(DEBUG, SAMPLER, "%s() line %u: ", \
		__func__ RTE_LOG_COMMA __LINE__, __VA_ARGS__)
#else
#define SAMPLER_LOG_DBG(...)
#endif

struct rte_sampler_stats {
	struct rte_sampler_stats_name *names;
	uint64_t *ids;
	uint64_t *values;
};

struct rte_sampler_source {
	TAILQ_ENTRY(rte_sampler_source) next;

	char name[SAMPLER_MAX_NAME_LEN];
	struct rte_sampler_source_ops ops;

	size_t capacity;
	struct rte_sampler_stats *stats;
};

struct rte_sampler_sink {
	TAILQ_ENTRY(rte_sampler_sink) next;

	char name[SAMPLER_MAX_NAME_LEN];
	struct rte_sampler_sink_ops ops;
};

TAILQ_HEAD(source_list, rte_sampler_source);
TAILQ_HEAD(sink_list, rte_sampler_sink);

struct rte_sampler_session {
	uint64_t duration;
	uint64_t interval;
	uint64_t start_time;
	uint64_t next_sample_time;

	struct source_list sources;
	uint64_t source_count;

	struct sink_list sinks;
	uint64_t sink_count;
};


static uint64_t sampler_us_to_tsc(uint64_t us)
{
	return (rte_get_timer_hz() / 1e6) * us;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_create, 25.11)
struct rte_sampler_session *
rte_sampler_session_create(uint64_t interval)
{
	struct rte_sampler_session *session;

	session = rte_zmalloc("sampler", sizeof(struct rte_sampler_session), 0);
	if (session == NULL) {
		SAMPLER_LOG_ERR("Unable to allocate memory for sampler session");
		rte_errno = ENOMEM;
		return NULL;
	}
	if (interval < SAMPLER_MIN_INTERVAL) {
		SAMPLER_LOG_ERR("Invalid interval: %lu", interval);
		rte_free(session);
		rte_errno = EINVAL;
		return NULL;
	}

	*session = (struct rte_sampler_session) {
		.interval = sampler_us_to_tsc(interval),
	};

	TAILQ_INIT(&session->sources);
	TAILQ_INIT(&session->sinks);
	return session;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_free, 25.11)
void
rte_sampler_session_free(struct rte_sampler_session *session)
{
	rte_free(session);
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_register_source, 25.11)
struct rte_sampler_source *
rte_sampler_session_register_source(struct rte_sampler_session *session,
	const char *source_name, struct rte_sampler_source_ops ops)
{
	if (session == NULL) {
		SAMPLER_LOG_ERR("Invalid parameters");
		rte_errno = EINVAL;
		return NULL;
	}

	struct rte_sampler_source *source = rte_zmalloc("sampler_source",
		sizeof(struct rte_sampler_source), 0);
	if (source == NULL) {
		SAMPLER_LOG_ERR("Unable to allocate memory for source");
		rte_errno = ENOMEM;
		return NULL;
	}

	rte_strscpy(source->name, source_name, SAMPLER_MAX_NAME_LEN);
	source->ops = ops;

	TAILQ_INSERT_TAIL(&session->sources, source, next);
	session->source_count++;

	return source;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_unregister_source, 25.11)
int
rte_sampler_session_unregister_source(struct rte_sampler_session *session,
	struct rte_sampler_source *source)
{
	if (source == NULL) {
		SAMPLER_LOG_ERR("Invalid source");
		rte_errno = EINVAL;
		return -EINVAL;
	}

	TAILQ_REMOVE(&session->sources, source, next);
	rte_free(source->stats);
	rte_free(source);
	session->source_count--;

	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_register_stats, 25.11)
int
rte_sampler_session_register_stats(__rte_unused struct rte_sampler_session *session,
	struct rte_sampler_source *source, struct rte_sampler_stats_name names[],
	const uint64_t ids[], size_t capacity)
{
	if (source->capacity > 0) {
		SAMPLER_LOG_ERR("Source already has stats registered");
		rte_errno = EEXIST;
		return -EEXIST;
	}

	size_t stat_size = sizeof(struct rte_sampler_stats_name) + sizeof(uint64_t) + sizeof(uint64_t);
	source->capacity = capacity;
	source->stats = rte_zmalloc("sampler_session_stats",
		capacity * stat_size, 0);
	if (source->stats == NULL) {
		SAMPLER_LOG_ERR("Unable to allocate memory for stats names");
		rte_errno = ENOMEM;
		return -ENOMEM;
	}

	for (uint64_t i = 0; i < capacity; i++) {
		rte_strscpy(source->stats->names[i].name, names[i].name, SAMPLER_MAX_NAME_LEN);
		source->stats->ids[i] = ids[i];
	}

	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_register_sink, 25.11)
struct rte_sampler_sink *
rte_sampler_session_register_sink(struct rte_sampler_session *session,
	const char *sink_name, struct rte_sampler_sink_ops ops)
{
	if (session == NULL) {
		SAMPLER_LOG_ERR("Invalid parameters");
		rte_errno = EINVAL;
		return NULL;
	}

	struct rte_sampler_sink *sink = rte_zmalloc("sampler_sink",
		sizeof(struct rte_sampler_sink), 0);
	if (sink == NULL) {
		SAMPLER_LOG_ERR("Unable to allocate memory for sink");
		rte_errno = ENOMEM;
		return NULL;
	}

	rte_strscpy(sink->name, sink_name, SAMPLER_MAX_NAME_LEN);
	sink->ops = ops;

	TAILQ_INSERT_TAIL(&session->sinks, sink, next);
	session->sink_count++;

	return sink;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_unregister_sink, 25.11)
int
rte_sampler_session_unregister_sink(struct rte_sampler_session *session,
	struct rte_sampler_sink *sink)
{
	if (sink == NULL) {
		SAMPLER_LOG_ERR("Invalid sink");
		rte_errno = EINVAL;
		return -EINVAL;
	}
	TAILQ_REMOVE(&session->sinks, sink, next);
	rte_free(sink);

	session->sink_count--;
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_start, 25.11)
int
rte_sampler_session_start(struct rte_sampler_session *session, uint64_t duration)
{
	uint64_t now = rte_rdtsc();

	if (session->start_time != 0) {
		SAMPLER_LOG_ERR("Sampler already started");
		rte_errno = EALREADY;
		return -EALREADY;
	}

	if (duration < session->interval) {
		SAMPLER_LOG_ERR("Duration %lu cannot be lesser than interval %lu",
			duration, session->interval);
		rte_errno = EINVAL;
		return -EINVAL;
	}

	session->duration = sampler_us_to_tsc(duration);
	session->start_time = now;
	session->next_sample_time = now + session->interval;
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_stop, 25.11)
int
rte_sampler_session_stop(struct rte_sampler_session *session)
{
	if (session->start_time == 0) {
		SAMPLER_LOG_ERR("Sampler already stopped");
		rte_errno = EALREADY;
		return -EALREADY;
	}

	memset(session, 0, sizeof(struct rte_sampler_session));
	return 0;
}

RTE_EXPORT_EXPERIMENTAL_SYMBOL(rte_sampler_session_process, 25.11)
int
rte_sampler_session_process(struct rte_sampler_session *session)
{
	uint64_t now = rte_rdtsc();
	struct rte_sampler_source *source;
	struct rte_sampler_sink *sink;

	if (unlikely(session->start_time == 0)) {
		SAMPLER_LOG_ERR("Sampler not started");
		rte_errno = EINVAL;
		return -EINVAL;
	}

	if (now < session->next_sample_time) {
		SAMPLER_LOG_DBG("Not time for next sample yet, current time: %lu, next sample time: %lu",
			now, session->next_sample_time);
		return 0;
	} else if (now - session->start_time > session->duration) {
		SAMPLER_LOG_INFO("Sampler duration exceeded, will not sample");
		rte_errno = ETIMEDOUT;
		return -ETIMEDOUT;
	}

	TAILQ_FOREACH(sink, &session->sinks, next) {
		int ret = sink->ops.report_begin(sink, session);
		if (ret < 0) {
			SAMPLER_LOG_ERR("Failed to begin report period for sink %s", sink->name);
			return ret;
		}
	}

	TAILQ_FOREACH(source, &session->sources, next) {
		struct rte_sampler_stats *stats = source->stats;
		int ret = source->ops.collect(source, session, stats->ids, stats->values, source->capacity);
		if (ret < 0) {
			SAMPLER_LOG_ERR("Failed to collect from source %s", source->name);
			return ret;
		} else if ((size_t)ret < source->capacity) {
			SAMPLER_LOG_DBG("Source returned (%lu) less than capacity (%lu) %s",
				source->name, ret, source->capacity);
			return ret;
		}
		ret = sink->ops.report_append(sink, session, stats->names, stats->values, source->capacity);
		if (ret < 0) {
			SAMPLER_LOG_ERR("Failed to report to sink %s", sink->name);
			return ret;
		}

	}

	TAILQ_FOREACH(sink, &session->sinks, next) {
		int ret = sink->ops.report_end(sink, session);
		if (ret < 0) {
			SAMPLER_LOG_ERR("Failed to begin report period for sink %s", sink->name);
			return ret;
		}
	}

	session->next_sample_time = now + session->interval;
	return 0;
}
