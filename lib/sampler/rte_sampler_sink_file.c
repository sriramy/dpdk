/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_sampler.h>
#include "rte_sampler_sink_file.h"

#define DEFAULT_BUFFER_SIZE 8192

/**
 * File sink user data structure
 */
struct file_sink_data {
	FILE *fp;
	enum rte_sampler_sink_file_format format;
	uint64_t sample_count;
	uint8_t header_written;
};

/**
 * Write CSV header
 */
static void
		write_csv_header(FILE *fp, const char *source_name __rte_unused,
		const struct rte_sampler_xstats_name *xstats_names,
		unsigned int n)
{
unsigned int i;

fprintf(fp, "timestamp,source_name,source_id");
for (i = 0; i < n; i++) {
fprintf(fp, ",%s", xstats_names[i].name);
}
fprintf(fp, "\n");
fflush(fp);
}

/**
 * Write in CSV format
 */
static int
write_csv_output(struct file_sink_data *data,
		const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *values,
		unsigned int n)
{
unsigned int i;
time_t now;
struct tm *tm_info;
char timestamp[64];

/* Get timestamp */
time(&now);
tm_info = localtime(&now);
strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

/* Write header on first sample */
if (!data->header_written && xstats_names != NULL) {
write_csv_header(data->fp, source_name, xstats_names, n);
data->header_written = 1;
}

/* Write data row */
fprintf(data->fp, "%s,%s,%u", timestamp, source_name, source_id);
for (i = 0; i < n; i++) {
fprintf(data->fp, ",%"PRIu64, values[i]);
}
fprintf(data->fp, "\n");
fflush(data->fp);

return 0;
}

/**
 * Write in JSON format
 */
static int
write_json_output(struct file_sink_data *data,
		const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n)
{
unsigned int i;
time_t now;

time(&now);

fprintf(data->fp, "{\n");
fprintf(data->fp, "  \"timestamp\": %ld,\n", now);
fprintf(data->fp, "  \"source_name\": \"%s\",\n", source_name);
fprintf(data->fp, "  \"source_id\": %u,\n", source_id);
fprintf(data->fp, "  \"sample_count\": %"PRIu64",\n", data->sample_count);
fprintf(data->fp, "  \"stats\": [\n");

for (i = 0; i < n; i++) {
fprintf(data->fp, "    {\n");
fprintf(data->fp, "      \"id\": %"PRIu64",\n", ids[i]);
if (xstats_names != NULL) {
fprintf(data->fp, "      \"name\": \"%s\",\n",
xstats_names[i].name);
}
fprintf(data->fp, "      \"value\": %"PRIu64"\n", values[i]);
fprintf(data->fp, "    }%s\n", (i < n - 1) ? "," : "");
}

fprintf(data->fp, "  ]\n");
fprintf(data->fp, "}\n");
fflush(data->fp);

return 0;
}

/**
 * Write in plain text format
 */
static int
write_text_output(struct file_sink_data *data,
		const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n)
{
unsigned int i;
time_t now;
struct tm *tm_info;
char timestamp[64];

time(&now);
tm_info = localtime(&now);
strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

fprintf(data->fp, "=== Sample #%"PRIu64" at %s ===\n",
data->sample_count, timestamp);
fprintf(data->fp, "Source: %s (ID=%u)\n", source_name, source_id);
fprintf(data->fp, "Statistics:\n");

for (i = 0; i < n; i++) {
if (xstats_names != NULL) {
fprintf(data->fp, "  [%"PRIu64"] %-50s : %"PRIu64"\n",
ids[i], xstats_names[i].name, values[i]);
} else {
fprintf(data->fp, "  [%u] ID=%"PRIu64" : %"PRIu64"\n",
i, ids[i], values[i]);
}
}
fprintf(data->fp, "\n");
fflush(data->fp);

return 0;
}

/**
 * File sink output callback
 */
static int
		file_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct file_sink_data *data = user_data;
int ret = 0;

if (data == NULL || data->fp == NULL)
return -EINVAL;

data->sample_count++;

switch (data->format) {
case RTE_SAMPLER_SINK_FILE_FORMAT_CSV:
ret = write_csv_output(data, source_name, source_id,
       xstats_names, values, n);
break;
case RTE_SAMPLER_SINK_FILE_FORMAT_JSON:
ret = write_json_output(data, source_name, source_id,
xstats_names, ids, values, n);
break;
case RTE_SAMPLER_SINK_FILE_FORMAT_TEXT:
ret = write_text_output(data, source_name, source_id,
xstats_names, ids, values, n);
break;
default:
ret = -EINVAL;
}

return ret;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_file_create)
struct rte_sampler_sink *
		rte_sampler_sink_file_create(struct rte_sampler_session *session,
		const char *name,
		const struct rte_sampler_sink_file_conf *conf)
{
struct rte_sampler_sink_ops ops;
struct file_sink_data *data;
struct rte_sampler_sink *sink;
const char *mode;

if (session == NULL || name == NULL || conf == NULL ||
    conf->filepath == NULL)
return NULL;

/* Allocate sink data */
data = rte_zmalloc(NULL, sizeof(*data), 0);
if (data == NULL)
return NULL;

/* Open file */
mode = conf->append ? "a" : "w";
data->fp = fopen(conf->filepath, mode);
if (data->fp == NULL) {
rte_free(data);
return NULL;
}

/* Set buffer size */
if (conf->buffer_size > 0) {
setvbuf(data->fp, NULL, _IOFBF, conf->buffer_size);
}

data->format = conf->format;
data->sample_count = 0;
data->header_written = 0;

/* Setup sink operations */
ops.output = file_sink_output;
ops.flags = 0;  /* Receive names by default */

/* Register sink */
sink = rte_sampler_session_register_sink(session, name, &ops, data);
if (sink == NULL) {
fclose(data->fp);
rte_free(data);
return NULL;
}

return sink;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_file_destroy)
int
		rte_sampler_sink_file_destroy(struct rte_sampler_sink *sink)
{
/* Note: This would need access to sink internals to get user_data
 * For now, cleanup is handled by sink_unregister */
if (sink == NULL)
return -EINVAL;

rte_sampler_sink_free(sink);
return 0;
}
