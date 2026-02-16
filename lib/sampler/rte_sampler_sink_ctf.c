/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_cycles.h>
#include <rte_string_fns.h>
#include <rte_sampler.h>
#include "rte_sampler_sink_ctf.h"

/**
 * CTF sink user data structure
 */
struct ctf_sink_data {
	FILE *metadata_fp;
	FILE *stream_fp;
	char trace_dir[256];
	char trace_name[64];
	uint64_t event_count;
	uint8_t metadata_written;
};

/**
 * Write CTF metadata (simplified version)
 */
static int
write_ctf_metadata(struct ctf_sink_data *data)
{
FILE *fp = data->metadata_fp;

if (data->metadata_written)
return 0;

/* CTF metadata header */
fprintf(fp, "/* CTF 1.8 */\n\n");

/* Typealias declarations */
		fprintf(fp, "typealias integer { size = 8; align = 8; signed = false; } := uint8_t;\n");
		fprintf(fp, "typealias integer { size = 16; align = 16; signed = false; } := uint16_t;\n");
		fprintf(fp, "typealias integer { size = 32; align = 32; signed = false; } := uint32_t;\n");
		fprintf(fp, "typealias integer { size = 64; align = 64; signed = false; } := uint64_t;\n\n");

/* Trace block */
fprintf(fp, "trace {\n");
fprintf(fp, "  major = 1;\n");
fprintf(fp, "  minor = 8;\n");
fprintf(fp, "  byte_order = le;\n");
fprintf(fp, "  packet.header := struct {\n");
		fprintf(fp, "    uint32_t magic;\n");
		fprintf(fp, "    uint64_t stream_id;\n");
fprintf(fp, "  };\n");
fprintf(fp, "};\n\n");

/* Clock */
fprintf(fp, "clock {\n");
fprintf(fp, "  name = monotonic;\n");
fprintf(fp, "  freq = 1000000000;\n");
fprintf(fp, "};\n\n");

/* Stream */
fprintf(fp, "stream {\n");
fprintf(fp, "  packet.context := struct {\n");
		fprintf(fp, "    uint64_t timestamp_begin;\n");
		fprintf(fp, "    uint64_t timestamp_end;\n");
		fprintf(fp, "    uint64_t events_discarded;\n");
fprintf(fp, "  };\n");
fprintf(fp, "  event.header := struct {\n");
		fprintf(fp, "    uint64_t timestamp;\n");
		fprintf(fp, "    uint32_t id;\n");
fprintf(fp, "  };\n");
fprintf(fp, "};\n\n");

/* Event definition */
fprintf(fp, "event {\n");
fprintf(fp, "  name = \"sampler_stats\";\n");
fprintf(fp, "  id = 0;\n");
fprintf(fp, "  fields := struct {\n");
fprintf(fp, "    string source_name;\n");
		fprintf(fp, "    uint16_t source_id;\n");
		fprintf(fp, "    uint32_t num_stats;\n");
		fprintf(fp, "    uint64_t stat_id;\n");
		fprintf(fp, "    uint64_t stat_value;\n");
fprintf(fp, "  };\n");
fprintf(fp, "};\n");

fflush(fp);
data->metadata_written = 1;

return 0;
}

/**
 * Write event to CTF stream (binary format - simplified)
 */
static int
write_ctf_event(struct ctf_sink_data *data,
const char *source_name,
uint16_t source_id,
const uint64_t *ids,
const uint64_t *values,
unsigned int n)
{
uint64_t timestamp;
uint32_t event_id = 0;
unsigned int i;
uint16_t name_len;

timestamp = rte_get_timer_cycles();

for (i = 0; i < n; i++) {
/* Event header */
		fwrite(&timestamp, sizeof(uint64_t), 1, data->stream_fp);
		fwrite(&event_id, sizeof(uint32_t), 1, data->stream_fp);

/* Event payload */
name_len = strlen(source_name) + 1;
		fwrite(&name_len, sizeof(uint16_t), 1, data->stream_fp);
fwrite(source_name, 1, name_len, data->stream_fp);
		fwrite(&source_id, sizeof(uint16_t), 1, data->stream_fp);
		fwrite(&n, sizeof(uint32_t), 1, data->stream_fp);
		fwrite(&ids[i], sizeof(uint64_t), 1, data->stream_fp);
		fwrite(&values[i], sizeof(uint64_t), 1, data->stream_fp);

data->event_count++;
}

fflush(data->stream_fp);
return 0;
}

/**
 * CTF sink output callback
 */
static int
		ctf_sink_output(const char *source_name,
uint16_t source_id,
const struct rte_sampler_xstats_name *xstats_names,
const uint64_t *ids,
const uint64_t *values,
unsigned int n,
void *user_data)
{
struct ctf_sink_data *data = user_data;

(void)xstats_names;  /* Names in metadata, not in stream */

if (data == NULL)
return -EINVAL;

/* Write metadata on first event */
if (!data->metadata_written) {
write_ctf_metadata(data);
}

/* Write events */
return write_ctf_event(data, source_name, source_id, ids, values, n);
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ctf_create)
struct rte_sampler_sink *
		rte_sampler_sink_ctf_create(struct rte_sampler_session *session,
		const char *name,
		const struct rte_sampler_sink_ctf_conf *conf)
{
struct rte_sampler_sink_ops ops;
struct ctf_sink_data *data;
struct rte_sampler_sink *sink;
char metadata_path[512];
char stream_path[512];

if (session == NULL || name == NULL || conf == NULL ||
    conf->trace_dir == NULL || conf->trace_name == NULL)
return NULL;

/* Allocate sink data */
data = rte_zmalloc(NULL, sizeof(*data), 0);
if (data == NULL)
return NULL;

/* Create trace directory */
mkdir(conf->trace_dir, 0755);

rte_strscpy(data->trace_dir, conf->trace_dir, sizeof(data->trace_dir));
rte_strscpy(data->trace_name, conf->trace_name, sizeof(data->trace_name));

/* Open metadata file */
snprintf(metadata_path, sizeof(metadata_path), "%s/metadata",
 conf->trace_dir);
data->metadata_fp = fopen(metadata_path, "w");
if (data->metadata_fp == NULL) {
rte_free(data);
return NULL;
}

/* Open stream file */
snprintf(stream_path, sizeof(stream_path), "%s/%s_0",
 conf->trace_dir, conf->trace_name);
data->stream_fp = fopen(stream_path, "wb");
if (data->stream_fp == NULL) {
fclose(data->metadata_fp);
rte_free(data);
return NULL;
}

data->event_count = 0;
data->metadata_written = 0;

/* Setup sink operations */
ops.output = ctf_sink_output;
ops.flags = RTE_SAMPLER_SINK_F_NO_NAMES;  /* Names in metadata */

/* Register sink */
sink = rte_sampler_session_register_sink(session, name, &ops, data);
if (sink == NULL) {
fclose(data->metadata_fp);
fclose(data->stream_fp);
rte_free(data);
return NULL;
}

return sink;
}

RTE_EXPORT_SYMBOL(rte_sampler_sink_ctf_destroy)
int
		rte_sampler_sink_ctf_destroy(struct rte_sampler_sink *sink)
{
if (sink == NULL)
return -EINVAL;

rte_sampler_sink_unregister(sink);
return 0;
}
