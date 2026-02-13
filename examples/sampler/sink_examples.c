/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

/**
 * Example: Demonstrating All Three Custom Sinks
 * 
 * Shows how to use:
 * 1. Ring buffer sink - stores in memory
 * 2. File sink - writes to files (CSV, JSON, text)
 * 3. CTF sink - writes in Common Trace Format
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_eventdev.h>
#include <rte_sampler.h>
#include <rte_sampler_eventdev.h>
#include <rte_sampler_sink_file.h>
#include <rte_sampler_sink_ringbuffer.h>
#include <rte_sampler_sink_ctf.h>

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
 * Demonstrate ring buffer sink
 */
static void
demo_ringbuffer_sink(struct rte_sampler_session *session)
{
struct rte_sampler_sink_ringbuffer_conf rb_conf;
struct rte_sampler_sink *rb_sink;
struct rte_sampler_ringbuffer_entry *entries;
int count, i, j;

printf("\n=== Ring Buffer Sink Demo ===\n");

/* Create ring buffer sink */
rb_conf.max_entries = 100;
rb_sink = rte_sampler_sink_ringbuffer_create(session, "ringbuffer", &rb_conf);
if (rb_sink == NULL) {
printf("Failed to create ring buffer sink\n");
return;
}

printf("Created ring buffer sink with max %u entries\n", rb_conf.max_entries);
printf("Ring buffer stores data in memory for later retrieval\n");

/* After some sampling, read from ring buffer */
rte_delay_ms(3000);

count = rte_sampler_sink_ringbuffer_count(rb_sink);
printf("\nRing buffer contains %d entries\n", count);

if (count > 0) {
entries = calloc(count, sizeof(struct rte_sampler_ringbuffer_entry));
if (entries != NULL) {
int read = rte_sampler_sink_ringbuffer_read(rb_sink, entries, count);
printf("Read %d entries from ring buffer:\n", read);

for (i = 0; i < read && i < 3; i++) {
printf("  Entry %d: %s (ID=%u) - %u stats\n",
       i, entries[i].source_name,
       entries[i].source_id,
       entries[i].num_stats);
for (j = 0; j < entries[i].num_stats && j < 5; j++) {
printf("    ID[%lu] = %lu\n",
       entries[i].ids[j],
       entries[i].values[j]);
}
}

/* Free allocated memory */
for (i = 0; i < read; i++) {
rte_free(entries[i].ids);
rte_free(entries[i].values);
}
free(entries);
}

/* Clear ring buffer */
rte_sampler_sink_ringbuffer_clear(rb_sink);
printf("Ring buffer cleared\n");
}

rte_sampler_sink_ringbuffer_destroy(rb_sink);
printf("Ring buffer sink destroyed\n");
}

/**
 * Demonstrate file sinks
 */
static void
demo_file_sinks(struct rte_sampler_session *session)
{
struct rte_sampler_sink_file_conf file_conf;
struct rte_sampler_sink *csv_sink, *json_sink, *text_sink;

printf("\n=== File Sink Demo ===\n");

/* Create CSV sink */
file_conf.filepath = "/tmp/sampler_output.csv";
file_conf.format = RTE_SAMPLER_SINK_FILE_FORMAT_CSV;
file_conf.buffer_size = 0;
file_conf.append = 0;

csv_sink = rte_sampler_sink_file_create(session, "csv_sink", &file_conf);
if (csv_sink != NULL) {
printf("Created CSV sink: %s\n", file_conf.filepath);
}

/* Create JSON sink */
file_conf.filepath = "/tmp/sampler_output.json";
file_conf.format = RTE_SAMPLER_SINK_FILE_FORMAT_JSON;

json_sink = rte_sampler_sink_file_create(session, "json_sink", &file_conf);
if (json_sink != NULL) {
printf("Created JSON sink: %s\n", file_conf.filepath);
}

/* Create text sink */
file_conf.filepath = "/tmp/sampler_output.txt";
file_conf.format = RTE_SAMPLER_SINK_FILE_FORMAT_TEXT;

text_sink = rte_sampler_sink_file_create(session, "text_sink", &file_conf);
if (text_sink != NULL) {
printf("Created text sink: %s\n", file_conf.filepath);
}

printf("\nFile sinks are writing data...\n");
rte_delay_ms(3000);

/* Cleanup */
if (csv_sink != NULL)
rte_sampler_sink_file_destroy(csv_sink);
if (json_sink != NULL)
rte_sampler_sink_file_destroy(json_sink);
if (text_sink != NULL)
rte_sampler_sink_file_destroy(text_sink);

printf("\nFiles written to /tmp/sampler_output.{csv,json,txt}\n");
}

/**
 * Demonstrate CTF sink
 */
static void
demo_ctf_sink(struct rte_sampler_session *session)
{
struct rte_sampler_sink_ctf_conf ctf_conf;
struct rte_sampler_sink *ctf_sink;

printf("\n=== CTF Sink Demo ===\n");

/* Create CTF sink */
ctf_conf.trace_dir = "/tmp/sampler_trace";
ctf_conf.trace_name = "sampler";

ctf_sink = rte_sampler_sink_ctf_create(session, "ctf_sink", &ctf_conf);
if (ctf_sink == NULL) {
printf("Failed to create CTF sink\n");
return;
}

printf("Created CTF sink: %s\n", ctf_conf.trace_dir);
printf("CTF traces can be viewed with babeltrace or Trace Compass\n");
printf("Writing trace data...\n");

rte_delay_ms(3000);

rte_sampler_sink_ctf_destroy(ctf_sink);
printf("\nCTF trace written to %s/\n", ctf_conf.trace_dir);
printf("View with: babeltrace %s\n", ctf_conf.trace_dir);
}

int
main(int argc, char **argv)
{
struct rte_sampler_session *session;
struct rte_sampler_session_conf session_conf;
struct rte_sampler_source *source;
struct rte_sampler_eventdev_conf eventdev_conf;
int ret;
uint8_t dev_id = 0;
unsigned int nb_eventdev;

/* Initialize EAL */
ret = rte_eal_init(argc, argv);
if (ret < 0)
rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

force_quit = false;
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);

printf("=== Sampler Sink Examples ===\n");
printf("Demonstrating: Ring Buffer, File (CSV/JSON/Text), and CTF sinks\n\n");

/* Check if eventdev is available */
nb_eventdev = rte_event_dev_count();
if (nb_eventdev == 0) {
printf("Note: No eventdev available - using mock data\n");
printf("Install eventdev-capable hardware for real data\n\n");
}

/* Create session */
session_conf.sample_interval_ms = 500;  /* Sample every 500ms */
session_conf.duration_ms = 0;           /* Infinite */
session_conf.name = "sink_demo";

session = rte_sampler_session_create(&session_conf);
if (session == NULL)
rte_exit(EXIT_FAILURE, "Failed to create session\n");

printf("Session created: %s\n", session_conf.name);

/* Register source if eventdev available */
if (nb_eventdev > 0) {
eventdev_conf.mode = RTE_SAMPLER_EVENTDEV_DEVICE;
eventdev_conf.queue_port_id = 0;

source = rte_sampler_eventdev_source_register(session, dev_id,
       &eventdev_conf);
if (source != NULL) {
printf("Registered eventdev source\n");
}
}

/* Start session */
rte_sampler_session_start(session);
printf("\nSession started - sampling every %lums\n",
       session_conf.sample_interval_ms);

/* Demonstrate each sink type */
demo_ringbuffer_sink(session);
demo_file_sinks(session);
demo_ctf_sink(session);

/* Keep sampling for demonstration */
printf("\n=== All sinks demonstrated ===\n");
printf("Press Ctrl+C to exit...\n\n");

while (!force_quit) {
rte_sampler_poll();
rte_delay_ms(100);
}

/* Cleanup */
printf("\nCleaning up...\n");
if (source != NULL && nb_eventdev > 0)
rte_sampler_source_unregister(source);
rte_sampler_session_free(session);
rte_eal_cleanup();

printf("\n=== Summary ===\n");
printf("1. Ring Buffer: Stores samples in memory (100 entries)\n");
printf("2. File Sinks: CSV, JSON, and text formats in /tmp/\n");
printf("3. CTF: Trace format in /tmp/sampler_trace/\n");
printf("\nExample completed successfully.\n");

return 0;
}
