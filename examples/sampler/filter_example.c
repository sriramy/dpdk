/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

/**
 * Example: Source Filtering by Stat Names
 * 
 * Demonstrates how to filter xstats by name patterns using wildcards.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_eventdev.h>
#include <rte_sampler.h>
#include <rte_sampler_eventdev.h>

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
 * Console sink for displaying filtered stats
 */
static int
		console_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
const char *filter_desc = (const char *)user_data;
unsigned int i;

printf("\n=== %s: %s (ID=%u) - %u stats ===\n",
       filter_desc, source_name, source_id, n);

for (i = 0; i < n && i < 10; i++) {
if (xstats_names != NULL) {
printf("  [%lu] %-50s : %lu\n",
       ids[i], xstats_names[i].name, values[i]);
} else {
printf("  [%lu] ID=%lu : %lu\n",
       i, ids[i], values[i]);
}
}
if (n > 10)
printf("  ... and %u more stats\n", n - 10);
printf("\n");

return 0;
}

int
main(int argc, char **argv)
{
struct rte_sampler_session *session;
struct rte_sampler_session_conf session_conf;
struct rte_sampler_source *source1, *source2, *source3;
struct rte_sampler_sink *sink1, *sink2, *sink3;
struct rte_sampler_sink_ops sink_ops;
struct rte_sampler_eventdev_conf eventdev_conf;
const char *pattern1[] = {"*rx*", "*tx*"};
const char *pattern2[] = {"*error*", "*drop*"};
const char *pattern3[] = {"*"};  /* All stats */
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

printf("=== Source Filtering Example ===\n");
printf("Demonstrates filtering xstats by name patterns\n\n");

/* Check if eventdev is available */
nb_eventdev = rte_event_dev_count();
if (nb_eventdev == 0) {
printf("No eventdev available.\n");
printf("This example demonstrates the filtering API.\n\n");
} else {
printf("Found %u eventdev device(s)\n\n", nb_eventdev);
}

/* Create session */
session_conf.sample_interval_ms = 1000;
session_conf.duration_ms = 0;
session_conf.name = "filter_demo";

session = rte_sampler_session_create(&session_conf);
if (session == NULL)
rte_exit(EXIT_FAILURE, "Failed to create session\n");

printf("Session created: %s\n\n", session_conf.name);

/*
 * Source 1: Filter for RX/TX stats only
 */
printf("--- Source 1: Filtering for *rx* and *tx* stats ---\n");

if (nb_eventdev > 0) {
eventdev_conf.mode = RTE_SAMPLER_EVENTDEV_DEVICE;
eventdev_conf.queue_port_id = 0;

source1 = rte_sampler_eventdev_source_register(session, dev_id,
       &eventdev_conf);
if (source1 == NULL) {
printf("Failed to register source 1\n");
goto cleanup;
}

/* Set filter for RX/TX stats */
ret = rte_sampler_source_set_filter(source1, pattern1, 2);
if (ret < 0) {
printf("Failed to set filter on source 1: %d\n", ret);
} else {
printf("Filter set: *rx* OR *tx*\n");
}

/* Register sink for source 1 */
sink_ops.output = console_sink_output;
sink_ops.flags = 0;
sink1 = rte_sampler_sink_register(session, "sink1", &sink_ops,
   (void *)"RX/TX Filter");
if (sink1 == NULL) {
printf("Failed to register sink 1\n");
}
}

/*
 * Source 2: Filter for error/drop stats only
 */
printf("\n--- Source 2: Filtering for *error* and *drop* stats ---\n");

if (nb_eventdev > 0) {
source2 = rte_sampler_eventdev_source_register(session, dev_id,
       &eventdev_conf);
if (source2 == NULL) {
printf("Failed to register source 2\n");
goto cleanup;
}

/* Set filter for error/drop stats */
ret = rte_sampler_source_set_filter(source2, pattern2, 2);
if (ret < 0) {
printf("Failed to set filter on source 2: %d\n", ret);
} else {
printf("Filter set: *error* OR *drop*\n");
}

/* Register sink for source 2 */
sink2 = rte_sampler_sink_register(session, "sink2", &sink_ops,
   (void *)"Error/Drop Filter");
if (sink2 == NULL) {
printf("Failed to register sink 2\n");
}
}

/*
 * Source 3: No filter (all stats)
 */
printf("\n--- Source 3: No filter (all stats) ---\n");

if (nb_eventdev > 0) {
source3 = rte_sampler_eventdev_source_register(session, dev_id,
       &eventdev_conf);
if (source3 == NULL) {
printf("Failed to register source 3\n");
goto cleanup;
}

printf("No filter set - will sample all available stats\n");

/* Register sink for source 3 */
sink3 = rte_sampler_sink_register(session, "sink3", &sink_ops,
   (void *)"No Filter (All)");
if (sink3 == NULL) {
printf("Failed to register sink 3\n");
}
}

printf("\n=== Filtering Summary ===\n");
printf("Source 1: Only samples stats matching *rx* or *tx*\n");
printf("Source 2: Only samples stats matching *error* or *drop*\n");
printf("Source 3: Samples all available stats (no filter)\n\n");

/* Start session */
rte_sampler_session_start(session);
printf("Starting sampling... Press Ctrl+C to exit\n\n");

/* Sampling loop */
int sample_count = 0;
while (!force_quit && sample_count < 5) {
rte_sampler_poll();
rte_delay_ms(100);
sample_count++;
}

/* Demonstrate clearing filter */
if (nb_eventdev > 0 && source1 != NULL) {
printf("\n--- Clearing filter on Source 1 ---\n");
ret = rte_sampler_source_clear_filter(source1);
if (ret == 0) {
printf("Filter cleared - now sampling all stats\n");
rte_delay_ms(2000);
}
}

/* Demonstrate getting active filters */
if (nb_eventdev > 0 && source2 != NULL) {
char *patterns[32];
ret = rte_sampler_source_get_filter(source2, patterns,
    32);
if (ret > 0) {
printf("\n--- Active filters on Source 2: %d patterns ---\n", ret);
for (int i = 0; i < ret && i < 32; i++) {
printf("  Pattern %d: %s\n", i, patterns[i]);
}
}
}

cleanup:
/* Cleanup */
printf("\nCleaning up...\n");
if (source1 != NULL)
rte_sampler_source_unregister(source1);
if (source2 != NULL)
rte_sampler_source_unregister(source2);
if (source3 != NULL)
rte_sampler_source_unregister(source3);
if (sink1 != NULL)
rte_sampler_sink_unregister(sink1);
if (sink2 != NULL)
rte_sampler_sink_unregister(sink2);
if (sink3 != NULL)
rte_sampler_sink_unregister(sink3);

rte_sampler_session_free(session);
rte_eal_cleanup();

printf("\n=== Filtering Features Demonstrated ===\n");
printf("1. Set filters with wildcards (* and ?)\n");
printf("2. Multiple patterns (OR logic)\n");
printf("3. Clear filters to sample all stats\n");
printf("4. Query active filters\n");
printf("\nExample completed successfully.\n");

return 0;
}
