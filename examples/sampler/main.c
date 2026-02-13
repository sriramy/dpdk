/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <stdio.h>
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

/* Console sink - prints stats to stdout */
static int
console_sink_output(const char *source_name,
    uint16_t source_id,
    const struct rte_sampler_xstats_name *xstats_names,
    const uint64_t *ids,
    const uint64_t *values,
    unsigned int n,
    void *user_data)
{
unsigned int i;

(void)user_data;

printf("\n=== %s (ID: %u) Statistics ===\n", source_name, source_id);
for (i = 0; i < n; i++) {
printf("  [%lu] %-50s : %20lu\n",
       ids[i], xstats_names[i].name, values[i]);
}
printf("\n");

return 0;
}

int
main(int argc, char **argv)
{
struct rte_sampler *sampler;
struct rte_sampler_sink_ops sink_ops;
struct rte_sampler_eventdev_conf eventdev_conf;
int ret, sink_handle, source_handle;
uint8_t dev_id = 0;
unsigned int nb_eventdev;

/* Initialize EAL */
ret = rte_eal_init(argc, argv);
if (ret < 0)
rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

force_quit = false;
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);

/* Check if eventdev is available */
nb_eventdev = rte_event_dev_count();
if (nb_eventdev == 0) {
printf("No eventdev available, example cannot run.\n");
printf("This is a demonstration of the sampler library API.\n");
rte_eal_cleanup();
return 0;
}

printf("Found %u eventdev device(s)\n", nb_eventdev);

/* Create sampler */
sampler = rte_sampler_create();
if (sampler == NULL)
rte_exit(EXIT_FAILURE, "Failed to create sampler\n");

printf("Sampler created successfully\n");

/* Setup console sink */
sink_ops.output = console_sink_output;
sink_handle = rte_sampler_sink_register(sampler, "console",
 &sink_ops, NULL);
if (sink_handle < 0) {
printf("Failed to register console sink: %d\n", sink_handle);
rte_sampler_free(sampler);
rte_eal_cleanup();
return -1;
}

printf("Console sink registered (handle: %d)\n", sink_handle);

/* Register eventdev source (device level) */
eventdev_conf.mode = RTE_SAMPLER_EVENTDEV_DEVICE;
eventdev_conf.queue_port_id = 0;

source_handle = rte_sampler_eventdev_source_register(sampler, dev_id,
      &eventdev_conf);
if (source_handle < 0) {
printf("Failed to register eventdev source: %d\n", source_handle);
rte_sampler_sink_unregister(sampler, sink_handle);
rte_sampler_free(sampler);
rte_eal_cleanup();
return -1;
}

printf("Eventdev source registered (handle: %d)\n", source_handle);
printf("\nStarting sampling (press Ctrl+C to stop)...\n");

/* Sampling loop */
while (!force_quit) {
ret = rte_sampler_sample(sampler);
if (ret < 0)
printf("Sampling error: %d\n", ret);

rte_delay_ms(2000);  /* Sample every 2 seconds */
}

printf("\nCleaning up...\n");

/* Cleanup */
rte_sampler_source_unregister(sampler, source_handle);
rte_sampler_sink_unregister(sampler, sink_handle);
rte_sampler_free(sampler);

rte_eal_cleanup();

printf("Sampler example completed.\n");
return 0;
}
