/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <string.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_log.h>
#include <rte_string_fns.h>
#include <rte_cycles.h>
#include <rte_sampler.h>

#define MAX_SESSIONS 32
#define MAX_SOURCES_PER_SESSION 64
#define MAX_SINKS_PER_SESSION 16
#define MAX_XSTATS_PER_SOURCE 256

/**
 * Sampler source structure
 */
struct rte_sampler_source {
struct rte_sampler_session *session;  /**< Back-pointer to session */
char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
uint16_t source_id;
struct rte_sampler_source_ops ops;
void *user_data;
struct rte_sampler_xstats_name xstats_names[MAX_XSTATS_PER_SOURCE];
uint64_t ids[MAX_XSTATS_PER_SOURCE];
uint64_t values[MAX_XSTATS_PER_SOURCE];
unsigned int xstats_count;
uint8_t valid;
};

/**
 * Sampler sink structure
 */
struct rte_sampler_sink {
struct rte_sampler_session *session;  /**< Back-pointer to session */
char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
struct rte_sampler_sink_ops ops;
void *user_data;
uint8_t valid;
};

/**
 * Sampler session structure
 */
struct rte_sampler_session {
char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
uint64_t sample_interval_ms;
uint64_t duration_ms;
uint64_t start_time;
uint64_t last_sample_time;
uint8_t active;
uint8_t valid;
struct rte_sampler_source sources[MAX_SOURCES_PER_SESSION];
struct rte_sampler_sink sinks[MAX_SINKS_PER_SESSION];
unsigned int num_sources;
unsigned int num_sinks;
};

/**
 * Global session registry
 */
static struct {
struct rte_sampler_session *sessions[MAX_SESSIONS];
unsigned int num_sessions;
} sampler_global;

RTE_EXPORT_SYMBOL(rte_sampler_session_create)
struct rte_sampler_session *
rte_sampler_session_create(const struct rte_sampler_session_conf *conf)
{
struct rte_sampler_session *session;
unsigned int i;

session = rte_zmalloc(NULL, sizeof(struct rte_sampler_session),
      RTE_CACHE_LINE_SIZE);
if (session == NULL)
return NULL;

/* Set configuration */
if (conf != NULL) {
session->sample_interval_ms = conf->sample_interval_ms;
session->duration_ms = conf->duration_ms;
if (conf->name != NULL)
rte_strscpy(session->name, conf->name, sizeof(session->name));
else
snprintf(session->name, sizeof(session->name), "session_%p", session);
} else {
/* Default configuration: manual sampling, infinite duration */
session->sample_interval_ms = 0;
session->duration_ms = 0;
snprintf(session->name, sizeof(session->name), "session_%p", session);
}

session->valid = 1;

/* Register session globally */
for (i = 0; i < MAX_SESSIONS; i++) {
if (sampler_global.sessions[i] == NULL) {
sampler_global.sessions[i] = session;
if (i >= sampler_global.num_sessions)
sampler_global.num_sessions = i + 1;
break;
}
}

if (i >= MAX_SESSIONS) {
RTE_LOG(WARNING, USER1,
"Maximum sessions (%d) reached, session will not be polled automatically\n",
MAX_SESSIONS);
}

return session;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_free)
void
rte_sampler_session_free(struct rte_sampler_session *session)
{
unsigned int i;

if (session == NULL)
return;

/* Stop session if active */
if (session->active)
rte_sampler_session_stop(session);

/* Unregister from global registry */
for (i = 0; i < sampler_global.num_sessions; i++) {
if (sampler_global.sessions[i] == session) {
sampler_global.sessions[i] = NULL;
break;
}
}

session->valid = 0;
rte_free(session);
}

RTE_EXPORT_SYMBOL(rte_sampler_session_start)
int
rte_sampler_session_start(struct rte_sampler_session *session)
{
if (session == NULL || !session->valid)
return -EINVAL;

session->active = 1;
session->start_time = rte_get_timer_cycles();
session->last_sample_time = session->start_time;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_stop)
int
rte_sampler_session_stop(struct rte_sampler_session *session)
{
if (session == NULL || !session->valid)
return -EINVAL;

session->active = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_session_is_active)
int
rte_sampler_session_is_active(struct rte_sampler_session *session)
{
uint64_t current_time, elapsed_ms;

if (session == NULL || !session->valid)
return -EINVAL;

if (!session->active)
return 0;

/* Check if duration has expired */
if (session->duration_ms > 0) {
current_time = rte_get_timer_cycles();
elapsed_ms = (current_time - session->start_time) * 1000 /
     rte_get_timer_hz();
if (elapsed_ms >= session->duration_ms) {
session->active = 0;
return 0;
}
}

return 1;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_free)
void
rte_sampler_source_free(struct rte_sampler_source *source)
{
if (source == NULL)
return;

source->valid = 0;
/* Note: source is part of session structure, not separately allocated */
}

RTE_EXPORT_SYMBOL(rte_sampler_source_register)
struct rte_sampler_source *
rte_sampler_source_register(struct rte_sampler_session *session,
     const char *source_name,
     uint16_t source_id,
     const struct rte_sampler_source_ops *ops,
     void *user_data)
{
struct rte_sampler_source *source;
unsigned int i;

if (session == NULL || !session->valid ||
    source_name == NULL || ops == NULL)
return NULL;

if (ops->xstats_names_get == NULL || ops->xstats_get == NULL)
return NULL;

/* Find free slot */
for (i = 0; i < MAX_SOURCES_PER_SESSION; i++) {
if (!session->sources[i].valid)
break;
}

if (i >= MAX_SOURCES_PER_SESSION)
return NULL;

source = &session->sources[i];
memset(source, 0, sizeof(*source));

source->session = session;
rte_strscpy(source->name, source_name, sizeof(source->name));
source->source_id = source_id;
source->ops = *ops;
source->user_data = user_data;
source->valid = 1;

if (i >= session->num_sources)
session->num_sources = i + 1;

return source;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_unregister)
int
rte_sampler_source_unregister(struct rte_sampler_source *source)
{
if (source == NULL || !source->valid)
return -EINVAL;

source->valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_free)
void
rte_sampler_sink_free(struct rte_sampler_sink *sink)
{
if (sink == NULL)
return;

sink->valid = 0;
/* Note: sink is part of session structure, not separately allocated */
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_register)
struct rte_sampler_sink *
rte_sampler_sink_register(struct rte_sampler_session *session,
   const char *sink_name,
   const struct rte_sampler_sink_ops *ops,
   void *user_data)
{
struct rte_sampler_sink *sink;
unsigned int i;

if (session == NULL || !session->valid ||
    sink_name == NULL || ops == NULL)
return NULL;

if (ops->output == NULL)
return NULL;

/* Find free slot */
for (i = 0; i < MAX_SINKS_PER_SESSION; i++) {
if (!session->sinks[i].valid)
break;
}

if (i >= MAX_SINKS_PER_SESSION)
return NULL;

sink = &session->sinks[i];
memset(sink, 0, sizeof(*sink));

sink->session = session;
rte_strscpy(sink->name, sink_name, sizeof(sink->name));
sink->ops = *ops;
sink->user_data = user_data;
sink->valid = 1;

if (i >= session->num_sinks)
session->num_sinks = i + 1;

return sink;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_unregister)
int
rte_sampler_sink_unregister(struct rte_sampler_sink *sink)
{
if (sink == NULL || !sink->valid)
return -EINVAL;

sink->valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sample)
int
rte_sampler_sample(struct rte_sampler_session *session)
{
unsigned int i, j;
int ret;

if (session == NULL || !session->valid)
return -EINVAL;

/* Sample from all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *source = &session->sources[i];

if (!source->valid)
continue;

/* Get xstats names if not already cached */
if (source->xstats_count == 0) {
ret = source->ops.xstats_names_get(
source->source_id,
source->xstats_names,
source->ids,
MAX_XSTATS_PER_SOURCE,
source->user_data);
if (ret < 0)
continue;

if (ret > MAX_XSTATS_PER_SOURCE) {
RTE_LOG(WARNING, USER1,
"Source %s (id=%u) has %d xstats, "
"but only %d can be sampled (truncated)\n",
source->name, source->source_id,
ret, MAX_XSTATS_PER_SOURCE);
}

source->xstats_count = ret < MAX_XSTATS_PER_SOURCE ? 
       ret : MAX_XSTATS_PER_SOURCE;
}

/* Get xstats values */
if (source->xstats_count > 0) {
ret = source->ops.xstats_get(
source->source_id,
source->ids,
source->values,
source->xstats_count,
source->user_data);
if (ret < 0)
continue;
}

/* Send to all sinks */
for (j = 0; j < session->num_sinks; j++) {
struct rte_sampler_sink *sink = &session->sinks[j];

if (!sink->valid)
continue;

ret = sink->ops.output(
source->name,
source->source_id,
source->xstats_names,
source->ids,
source->values,
source->xstats_count,
sink->user_data);
if (ret < 0) {
/* Continue with other sinks on error */
continue;
}
}
}

/* Update last sample time */
session->last_sample_time = rte_get_timer_cycles();

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_poll)
int
rte_sampler_poll(void)
{
unsigned int i;
int polled = 0;
uint64_t current_time, elapsed_ms;

for (i = 0; i < sampler_global.num_sessions; i++) {
struct rte_sampler_session *session = sampler_global.sessions[i];

if (session == NULL || !session->valid)
continue;

/* Skip inactive sessions */
if (!rte_sampler_session_is_active(session))
continue;

/* Skip manual sessions (interval == 0) */
if (session->sample_interval_ms == 0)
continue;

/* Check if it's time to sample */
current_time = rte_get_timer_cycles();
elapsed_ms = (current_time - session->last_sample_time) * 1000 /
     rte_get_timer_hz();

if (elapsed_ms >= session->sample_interval_ms) {
rte_sampler_sample(session);
polled++;
}
}

return polled;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_names_get)
int
rte_sampler_xstats_names_get(struct rte_sampler_session *session,
      struct rte_sampler_source *source,
      struct rte_sampler_xstats_name *xstats_names,
      unsigned int size)
{
unsigned int total = 0;
unsigned int i, j;

if (session == NULL || !session->valid)
return -EINVAL;

/* Get from specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

if (xstats_names == NULL)
return source->xstats_count;

for (i = 0; i < source->xstats_count && i < size; i++) {
xstats_names[i] = source->xstats_names[i];
}

return i;
}

/* Get from all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

if (xstats_names != NULL) {
for (j = 0; j < src->xstats_count && 
     total + j < size; j++) {
xstats_names[total + j] = src->xstats_names[j];
}
}

total += src->xstats_count;
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_get)
int
rte_sampler_xstats_get(struct rte_sampler_session *session,
       struct rte_sampler_source *source,
       const uint64_t *ids,
       uint64_t *values,
       unsigned int n)
{
unsigned int total = 0;
unsigned int i, j;

if (session == NULL || !session->valid || values == NULL)
return -EINVAL;

/* Get from specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

/* If ids is NULL, return all values */
if (ids == NULL) {
for (i = 0; i < source->xstats_count && i < n; i++) {
values[i] = source->values[i];
}
return i;
}

/* Get specific ids */
for (i = 0; i < n; i++) {
for (j = 0; j < source->xstats_count; j++) {
if (source->ids[j] == ids[i]) {
values[i] = source->values[j];
break;
}
}
}

return n;
}

/* Get from all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

for (j = 0; j < src->xstats_count; j++) {
if (total >= n)
break;
values[total++] = src->values[j];
}
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_reset)
int
rte_sampler_xstats_reset(struct rte_sampler_session *session,
  struct rte_sampler_source *source,
  const uint64_t *ids,
  unsigned int n)
{
unsigned int i;
int ret;

if (session == NULL || !session->valid)
return -EINVAL;

/* Reset specific source */
if (source != NULL) {
if (!source->valid || source->session != session)
return -EINVAL;

if (source->ops.xstats_reset != NULL) {
ret = source->ops.xstats_reset(
source->source_id,
ids,
n,
source->user_data);
if (ret < 0)
return ret;
}

/* Clear cached values */
memset(source->values, 0, sizeof(source->values));

return 0;
}

/* Reset all sources */
for (i = 0; i < session->num_sources; i++) {
struct rte_sampler_source *src = &session->sources[i];

if (!src->valid)
continue;

if (src->ops.xstats_reset != NULL) {
ret = src->ops.xstats_reset(
src->source_id,
ids,
n,
src->user_data);
/* Continue with other sources on error */
}

/* Clear cached values */
memset(src->values, 0, sizeof(src->values));
}

return 0;
}
