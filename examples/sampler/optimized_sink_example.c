/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

/**
 * Example: Optimized Sink - Avoid Passing Large Name Arrays
 * 
 * This example demonstrates using RTE_SAMPLER_SINK_F_NO_NAMES flag
 * to avoid the overhead of passing stat names on every sample.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <rte_eal.h>
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
 * Regular sink - receives names every time (simple but slower)
 */
static int
		regular_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
unsigned int i;

printf("[Regular Sink] Source %s (ID=%u) - Received %u stats WITH names\n",
       source_name, source_id, n);

/* Can directly use xstats_names array */
for (i = 0; i < n && i < 3; i++) {
printf("  %s = %lu\n", xstats_names[i].name, values[i]);
}
if (n > 3)
printf("  ... and %u more stats\n", n - 3);

return 0;
}

/**
 * Optimized sink - doesn't receive names (faster)
 * Uses on-demand lookup only when needed
 */
struct optimized_sink_data {
	struct rte_sampler_source *source;  /* Cached source pointer */
	unsigned int sample_count;
};

static int
		optimized_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct optimized_sink_data *data = user_data;
unsigned int i;

data->sample_count++;

printf("[Optimized Sink] Source %s (ID=%u) - Received %u stats WITHOUT names "
       "(sample #%u)\n",
       source_name, source_id, n, data->sample_count);

/* xstats_names is NULL - we didn't receive names! */
if (xstats_names != NULL) {
printf("  ERROR: Expected NULL xstats_names but got data!\n");
return -1;
}

/* Option 1: Just use IDs and values (most efficient) */
for (i = 0; i < n && i < 3; i++) {
printf("  ID[%lu] = %lu\n", ids[i], values[i]);
}

/* Option 2: If we really need a name, look it up on-demand */
if (data->sample_count == 1 && data->source != NULL) {
/* Only on first sample, demonstrate lookup */
struct rte_sampler_xstats_name name;
if (rte_sampler_source_get_xstats_name(data->source, ids[0], &name) == 0) {
printf("  (On-demand lookup: ID[%lu] name is '%s')\n",
       ids[0], name.name);
}
}

if (n > 3)
printf("  ... and %u more stats\n", n - 3);

return 0;
}

int
main(int argc, char **argv)
{
struct rte_sampler_session *session;
struct rte_sampler_session_conf session_conf;
struct rte_sampler_source *source;
struct rte_sampler_sink *regular_sink, *optimized_sink;
struct rte_sampler_sink_ops regular_ops, optimized_ops;
struct optimized_sink_data *opt_data;
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

printf("=== Optimized Sink Example ===\n");
printf("Demonstrates avoiding large name array transfers\n\n");

/* Check if eventdev is available */
nb_eventdev = rte_event_dev_count();
if (nb_eventdev == 0) {
printf("No eventdev available.\n");
printf("This example shows the API usage even without eventdev.\n\n");
/* Continue anyway to demonstrate the API */
}

/* Create session */
session_conf.sample_interval_ms = 1000;
session_conf.duration_ms = 5000;  /* 5 seconds */
session_conf.name = "optimization_demo";

session = rte_sampler_session_create(&session_conf);
if (session == NULL)
rte_exit(EXIT_FAILURE, "Failed to create session\n");

printf("Session created: %s\n\n", session_conf.name);

/* Register source if eventdev available */
if (nb_eventdev > 0) {
eventdev_conf.mode = RTE_SAMPLER_EVENTDEV_DEVICE;
eventdev_conf.queue_port_id = 0;

source = rte_sampler_eventdev_source_register(session, dev_id,
       &eventdev_conf);
if (source == NULL) {
printf("Warning: Failed to register eventdev source\n");
source = NULL;
} else {
printf("Registered eventdev source\n");
}
} else {
source = NULL;
}

/*
 * Register REGULAR sink (receives names every time)
 */
regular_ops.output = regular_sink_output;
regular_ops.flags = 0;  /* No optimization flags */

regular_sink = rte_sampler_sink_register(session, "regular_sink",
  &regular_ops, NULL);
if (regular_sink == NULL) {
printf("Failed to register regular sink\n");
goto cleanup;
}

printf("Registered regular sink (receives names every sample)\n");

/*
 * Register OPTIMIZED sink (doesn't receive names)
 */
opt_data = malloc(sizeof(*opt_data));
if (opt_data == NULL) {
printf("Failed to allocate optimized sink data\n");
goto cleanup;
}
opt_data->source = source;
opt_data->sample_count = 0;

optimized_ops.output = optimized_sink_output;
optimized_ops.flags = RTE_SAMPLER_SINK_F_NO_NAMES;  /* OPTIMIZATION! */

optimized_sink = rte_sampler_sink_register(session, "optimized_sink",
    &optimized_ops, opt_data);
if (optimized_sink == NULL) {
printf("Failed to register optimized sink\n");
free(opt_data);
goto cleanup;
}

printf("Registered optimized sink (NO names passed - saves bandwidth!)\n\n");

printf("Performance Note:\n");
printf("  Regular sink: Receives up to 256 names × 128 bytes = 32KB per sample\n");
printf("  Optimized sink: Receives only IDs (256 × 8 bytes = 2KB) - 94%% less data!\n\n");

printf("Starting sampling...\n\n");

/* Start session */
rte_sampler_session_start(session);

/* Polling loop */
while (!force_quit && rte_sampler_session_is_active(session)) {
rte_sampler_poll();
rte_delay_ms(100);
}

printf("\n=== Summary ===\n");
printf("Regular sink always received full name arrays.\n");
printf("Optimized sink received NULL for names (saved memory bandwidth).\n");
printf("Optimized sink can still lookup names on-demand if needed.\n");

cleanup:
/* Cleanup */
if (optimized_sink != NULL) {
rte_sampler_sink_unregister(optimized_sink);
free(opt_data);
}
if (regular_sink != NULL)
rte_sampler_sink_unregister(regular_sink);
if (source != NULL)
rte_sampler_source_unregister(source);

rte_sampler_session_free(session);
rte_eal_cleanup();

printf("\nExample completed.\n");
return 0;
}
