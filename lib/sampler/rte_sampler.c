/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <string.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_sampler.h>

#define MAX_SOURCES 64
#define MAX_SINKS 16
#define MAX_XSTATS_PER_SOURCE 256

/**
 * Internal source structure
 */
struct sampler_source {
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
 * Internal sink structure
 */
struct sampler_sink {
char name[RTE_SAMPLER_XSTATS_NAME_SIZE];
struct rte_sampler_sink_ops ops;
void *user_data;
uint8_t valid;
};

/**
 * Sampler context structure
 */
struct rte_sampler {
struct sampler_source sources[MAX_SOURCES];
struct sampler_sink sinks[MAX_SINKS];
unsigned int num_sources;
unsigned int num_sinks;
};

RTE_EXPORT_SYMBOL(rte_sampler_create)
struct rte_sampler *
rte_sampler_create(void)
{
struct rte_sampler *sampler;

sampler = rte_zmalloc(NULL, sizeof(struct rte_sampler),
      RTE_CACHE_LINE_SIZE);
if (sampler == NULL)
return NULL;

return sampler;
}

RTE_EXPORT_SYMBOL(rte_sampler_free)
void
rte_sampler_free(struct rte_sampler *sampler)
{
rte_free(sampler);
}

RTE_EXPORT_SYMBOL(rte_sampler_source_register)
int
rte_sampler_source_register(struct rte_sampler *sampler,
     const char *source_name,
     uint16_t source_id,
     const struct rte_sampler_source_ops *ops,
     void *user_data)
{
struct sampler_source *source;
unsigned int i;

if (sampler == NULL || source_name == NULL || ops == NULL)
return -EINVAL;

if (ops->xstats_names_get == NULL || ops->xstats_get == NULL)
return -EINVAL;

/* Find free slot */
for (i = 0; i < MAX_SOURCES; i++) {
if (!sampler->sources[i].valid)
break;
}

if (i >= MAX_SOURCES)
return -ENOSPC;

source = &sampler->sources[i];
memset(source, 0, sizeof(*source));

rte_strscpy(source->name, source_name, sizeof(source->name));
source->source_id = source_id;
source->ops = *ops;
source->user_data = user_data;
source->valid = 1;

if (i >= sampler->num_sources)
sampler->num_sources = i + 1;

return i;
}

RTE_EXPORT_SYMBOL(rte_sampler_source_unregister)
int
rte_sampler_source_unregister(struct rte_sampler *sampler,
       int source_handle)
{
if (sampler == NULL || source_handle < 0 || 
    (unsigned int)source_handle >= MAX_SOURCES)
return -EINVAL;

if (!sampler->sources[source_handle].valid)
return -ENOENT;

sampler->sources[source_handle].valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_register)
int
rte_sampler_sink_register(struct rte_sampler *sampler,
   const char *sink_name,
   const struct rte_sampler_sink_ops *ops,
   void *user_data)
{
struct sampler_sink *sink;
unsigned int i;

if (sampler == NULL || sink_name == NULL || ops == NULL)
return -EINVAL;

if (ops->output == NULL)
return -EINVAL;

/* Find free slot */
for (i = 0; i < MAX_SINKS; i++) {
if (!sampler->sinks[i].valid)
break;
}

if (i >= MAX_SINKS)
return -ENOSPC;

sink = &sampler->sinks[i];
memset(sink, 0, sizeof(*sink));

rte_strscpy(sink->name, sink_name, sizeof(sink->name));
sink->ops = *ops;
sink->user_data = user_data;
sink->valid = 1;

if (i >= sampler->num_sinks)
sampler->num_sinks = i + 1;

return i;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_unregister)
int
rte_sampler_sink_unregister(struct rte_sampler *sampler,
     int sink_handle)
{
if (sampler == NULL || sink_handle < 0 || 
    (unsigned int)sink_handle >= MAX_SINKS)
return -EINVAL;

if (!sampler->sinks[sink_handle].valid)
return -ENOENT;

sampler->sinks[sink_handle].valid = 0;

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_sample)
int
rte_sampler_sample(struct rte_sampler *sampler)
{
unsigned int i, j;
int ret;

if (sampler == NULL)
return -EINVAL;

/* Sample from all sources */
for (i = 0; i < sampler->num_sources; i++) {
struct sampler_source *source = &sampler->sources[i];

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
for (j = 0; j < sampler->num_sinks; j++) {
struct sampler_sink *sink = &sampler->sinks[j];

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

return 0;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_names_get)
int
rte_sampler_xstats_names_get(struct rte_sampler *sampler,
      int source_handle,
      struct rte_sampler_xstats_name *xstats_names,
      unsigned int size)
{
struct sampler_source *source;
unsigned int total = 0;
unsigned int i, j;

if (sampler == NULL)
return -EINVAL;

/* Get from specific source */
if (source_handle >= 0) {
if ((unsigned int)source_handle >= MAX_SOURCES)
return -EINVAL;

source = &sampler->sources[source_handle];
if (!source->valid)
return -ENOENT;

if (xstats_names == NULL)
return source->xstats_count;

for (i = 0; i < source->xstats_count && i < size; i++) {
xstats_names[i] = source->xstats_names[i];
}

return i;
}

/* Get from all sources */
for (i = 0; i < sampler->num_sources; i++) {
source = &sampler->sources[i];

if (!source->valid)
continue;

if (xstats_names != NULL) {
for (j = 0; j < source->xstats_count && 
     total + j < size; j++) {
xstats_names[total + j] = source->xstats_names[j];
}
}

total += source->xstats_count;
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_get)
int
rte_sampler_xstats_get(struct rte_sampler *sampler,
       int source_handle,
       const uint64_t *ids,
       uint64_t *values,
       unsigned int n)
{
struct sampler_source *source;
unsigned int total = 0;
unsigned int i, j;

if (sampler == NULL || values == NULL)
return -EINVAL;

/* Get from specific source */
if (source_handle >= 0) {
if ((unsigned int)source_handle >= MAX_SOURCES)
return -EINVAL;

source = &sampler->sources[source_handle];
if (!source->valid)
return -ENOENT;

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
for (i = 0; i < sampler->num_sources; i++) {
source = &sampler->sources[i];

if (!source->valid)
continue;

for (j = 0; j < source->xstats_count && total + j < n; j++) {
values[total + j] = source->values[j];
}

total += source->xstats_count;
}

return total;
}

RTE_EXPORT_SYMBOL(rte_sampler_xstats_reset)
int
rte_sampler_xstats_reset(struct rte_sampler *sampler,
  int source_handle,
  const uint64_t *ids,
  unsigned int n)
{
struct sampler_source *source;
unsigned int i;
int ret;

if (sampler == NULL)
return -EINVAL;

/* Reset specific source */
if (source_handle >= 0) {
if ((unsigned int)source_handle >= MAX_SOURCES)
return -EINVAL;

source = &sampler->sources[source_handle];
if (!source->valid)
return -ENOENT;

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
for (i = 0; i < sampler->num_sources; i++) {
source = &sampler->sources[i];

if (!source->valid)
continue;

if (source->ops.xstats_reset != NULL) {
ret = source->ops.xstats_reset(
source->source_id,
ids,
n,
source->user_data);
/* Continue with other sources on error */
}

/* Clear cached values */
memset(source->values, 0, sizeof(source->values));
}

return 0;
}
