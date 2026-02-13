/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

/**
 * Example: Custom Source and Sink with Custom Sampler IDs
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_sampler.h>

static volatile bool force_quit;

static void
signal_handler(int signum)
{
if (signum == SIGINT || signum == SIGTERM) {
printf("\nSignal %d received, preparing to exit...\n", signum);
force_quit = true;
}
}

/**
 * Custom source data structure
 * Store your custom sampler ID here
 */
struct my_source_data {
uint64_t custom_sampler_id;
uint64_t packet_count;
uint64_t byte_count;
uint64_t error_count;
};

/**
 * Custom sink data structure
 * Store ID mapping or other sink state
 */
struct my_sink_data {
FILE *output_file;
/* Mapping from source_id to custom_sampler_id */
struct {
uint16_t source_id;
uint64_t sampler_id;
} id_map[10];
unsigned int num_mappings;
};

/**
 * Source callback: Get stat names and IDs
 */
static int
my_source_xstats_names_get(uint16_t source_id,
   struct rte_sampler_xstats_name *xstats_names,
   uint64_t *ids,
   unsigned int size,
   void *user_data)
{
struct my_source_data *data = user_data;
const int num_stats = 3;

/* Return count if querying size */
if (xstats_names == NULL)
return num_stats;

/* Embed custom_sampler_id in stat names */
if (size >= 1) {
snprintf(xstats_names[0].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
 "sampler_%lu_packets", data->custom_sampler_id);
ids[0] = 0;
}
if (size >= 2) {
snprintf(xstats_names[1].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
 "sampler_%lu_bytes", data->custom_sampler_id);
ids[1] = 1;
}
if (size >= 3) {
snprintf(xstats_names[2].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
 "sampler_%lu_errors", data->custom_sampler_id);
ids[2] = 2;
}

return num_stats;
}

/**
 * Source callback: Get stat values
 */
static int
my_source_xstats_get(uint16_t source_id,
     const uint64_t *ids,
     uint64_t *values,
     unsigned int n,
     void *user_data)
{
struct my_source_data *data = user_data;
unsigned int i;

/* Simulate incrementing counters */
data->packet_count++;
data->byte_count += 64;
if (data->packet_count % 100 == 0)
data->error_count++;

/* Return values for requested IDs */
for (i = 0; i < n; i++) {
switch (ids[i]) {
case 0:
values[i] = data->packet_count;
break;
case 1:
values[i] = data->byte_count;
break;
case 2:
values[i] = data->error_count;
break;
default:
values[i] = 0;
}
}

return n;
}

/**
 * Source callback: Reset stats (optional)
 */
static int
my_source_xstats_reset(uint16_t source_id,
       const uint64_t *ids,
       unsigned int n,
       void *user_data)
{
struct my_source_data *data = user_data;

data->packet_count = 0;
data->byte_count = 0;
data->error_count = 0;

return 0;
}

/**
 * Sink callback: Process sampled data
 */
static int
my_sink_output(const char *source_name,
       uint16_t source_id,
       const struct rte_sampler_xstats_name *xstats_names,
       const uint64_t *ids,
       const uint64_t *values,
       unsigned int n,
       void *user_data)
{
struct my_sink_data *sink_data = user_data;
uint64_t sampler_id = 0;
unsigned int i;

/* Look up custom sampler_id using source_id */
for (i = 0; i < sink_data->num_mappings; i++) {
if (sink_data->id_map[i].source_id == source_id) {
sampler_id = sink_data->id_map[i].sampler_id;
break;
}
}

/* Write to file using sampler_id instead of source_id */
fprintf(sink_data->output_file, 
"\n=== Custom Sampler ID: %lu (Source: %s, source_id=%u) ===\n",
sampler_id, source_name, source_id);

for (i = 0; i < n; i++) {
fprintf(sink_data->output_file, "  [%lu] %-40s : %lu\n",
ids[i], xstats_names[i].name, values[i]);
}

fflush(sink_data->output_file);

/* Also print to console */
printf("Sampler %lu: packets=%lu, bytes=%lu, errors=%lu\n",
       sampler_id, n >= 1 ? values[0] : 0,
       n >= 2 ? values[1] : 0, n >= 3 ? values[2] : 0);

return 0;
}

int
main(int argc, char **argv)
{
struct rte_sampler_session *session;
struct rte_sampler_session_conf session_conf;
struct rte_sampler_source_ops source_ops;
struct rte_sampler_sink_ops sink_ops;
struct my_source_data *source_data1, *source_data2;
struct my_sink_data *sink_data;
struct rte_sampler_source *source1, *source2;
struct rte_sampler_sink *sink;
int ret;

/* Initialize EAL */
ret = rte_eal_init(argc, argv);
if (ret < 0)
rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

force_quit = false;
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);

printf("=== Custom Source and Sink Example ===\n\n");

/* Create session */
session_conf.sample_interval_ms = 1000;  /* Sample every 1 second */
session_conf.duration_ms = 10000;        /* Run for 10 seconds */
session_conf.name = "custom_example";

session = rte_sampler_session_create(&session_conf);
if (session == NULL)
rte_exit(EXIT_FAILURE, "Failed to create session\n");

printf("Session created: %s\n", session_conf.name);

/* Setup source operations */
source_ops.xstats_names_get = my_source_xstats_names_get;
source_ops.xstats_get = my_source_xstats_get;
source_ops.xstats_reset = my_source_xstats_reset;

/* 
 * Create Source 1 with custom sampler ID 1001
 */
source_data1 = malloc(sizeof(*source_data1));
if (source_data1 == NULL)
rte_exit(EXIT_FAILURE, "Failed to allocate source data\n");

source_data1->custom_sampler_id = 1001;  /* YOUR CUSTOM ID */
source_data1->packet_count = 0;
source_data1->byte_count = 0;
source_data1->error_count = 0;

source1 = rte_sampler_source_register(session,
      "my_source_1",
      0,  /* source_id */
      &source_ops,
      source_data1);
if (source1 == NULL) {
free(source_data1);
rte_exit(EXIT_FAILURE, "Failed to register source 1\n");
}

printf("Registered source 1 with custom_sampler_id=%lu\n",
       source_data1->custom_sampler_id);

/*
 * Create Source 2 with custom sampler ID 2002
 */
source_data2 = malloc(sizeof(*source_data2));
if (source_data2 == NULL) {
rte_sampler_source_unregister(source1);
free(source_data1);
rte_exit(EXIT_FAILURE, "Failed to allocate source data\n");
}

source_data2->custom_sampler_id = 2002;  /* DIFFERENT CUSTOM ID */
source_data2->packet_count = 0;
source_data2->byte_count = 0;
source_data2->error_count = 0;

source2 = rte_sampler_source_register(session,
      "my_source_2",
      1,  /* different source_id */
      &source_ops,
      source_data2);
if (source2 == NULL) {
rte_sampler_source_unregister(source1);
free(source_data1);
free(source_data2);
rte_exit(EXIT_FAILURE, "Failed to register source 2\n");
}

printf("Registered source 2 with custom_sampler_id=%lu\n",
       source_data2->custom_sampler_id);

/*
 * Create sink with ID mapping
 */
sink_data = malloc(sizeof(*sink_data));
if (sink_data == NULL) {
rte_sampler_source_unregister(source1);
rte_sampler_source_unregister(source2);
free(source_data1);
free(source_data2);
rte_exit(EXIT_FAILURE, "Failed to allocate sink data\n");
}

sink_data->output_file = fopen("custom_sampler_output.txt", "w");
if (sink_data->output_file == NULL) {
rte_sampler_source_unregister(source1);
rte_sampler_source_unregister(source2);
free(source_data1);
free(source_data2);
free(sink_data);
rte_exit(EXIT_FAILURE, "Failed to open output file\n");
}

/* Build ID mapping: source_id -> custom_sampler_id */
sink_data->num_mappings = 2;
sink_data->id_map[0].source_id = 0;
sink_data->id_map[0].sampler_id = source_data1->custom_sampler_id;
sink_data->id_map[1].source_id = 1;
sink_data->id_map[1].sampler_id = source_data2->custom_sampler_id;

sink_ops.output = my_sink_output;

sink = rte_sampler_sink_register(session,
  "my_sink",
  &sink_ops,
  sink_data);
if (sink == NULL) {
fclose(sink_data->output_file);
rte_sampler_source_unregister(source1);
rte_sampler_source_unregister(source2);
free(source_data1);
free(source_data2);
free(sink_data);
rte_exit(EXIT_FAILURE, "Failed to register sink\n");
}

printf("Registered sink with ID mapping\n");
printf("\nStarting sampling (10 seconds)...\n\n");

/* Start session */
rte_sampler_session_start(session);

/* Automatic polling loop */
while (!force_quit && rte_sampler_session_is_active(session)) {
rte_sampler_poll();
rte_delay_ms(100);
}

printf("\nSampling complete. Check 'custom_sampler_output.txt' for details.\n");

/* Cleanup */
printf("\nCleaning up...\n");
rte_sampler_source_unregister(source1);
rte_sampler_source_unregister(source2);
rte_sampler_sink_unregister(sink);
fclose(sink_data->output_file);
free(source_data1);
free(source_data2);
free(sink_data);
rte_sampler_session_free(session);

rte_eal_cleanup();

printf("Example completed successfully.\n");
return 0;
}
