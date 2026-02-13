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
struct rte_sampler_session *session1, *session2;
struct rte_sampler_session_conf session_conf;
struct rte_sampler_sink_ops sink_ops;
struct rte_sampler_eventdev_conf eventdev_conf;
struct rte_sampler_source *source1, *source2;
struct rte_sampler_sink *sink1, *sink2;
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

/* Check if eventdev is available */
nb_eventdev = rte_event_dev_count();
if (nb_eventdev == 0) {
printf("No eventdev available, example cannot run.\n");
printf("This is a demonstration of the sampler library API.\n");
rte_eal_cleanup();
return 0;
}

printf("Found %u eventdev device(s)\n", nb_eventdev);
printf("Creating multiple sampling sessions with different intervals...\n\n");

/* Create Session 1: Fast sampling every 1 second, runs for 10 seconds */
session_conf.sample_interval_ms = 1000;
session_conf.duration_ms = 10000;
session_conf.name = "fast_session";

session1 = rte_sampler_session_create(&session_conf);
if (session1 == NULL)
rte_exit(EXIT_FAILURE, "Failed to create session 1\n");

printf("Session 1 created: %s (interval=1s, duration=10s)\n",
       session_conf.name);

/* Setup console sink for session 1 */
sink_ops.output = console_sink_output;
sink1 = rte_sampler_sink_register(session1, "console_fast",
   &sink_ops, NULL);
if (sink1 == NULL) {
printf("Failed to register sink for session 1\n");
goto cleanup_session1;
}

/* Register eventdev source to session 1 (device level) */
eventdev_conf.mode = RTE_SAMPLER_EVENTDEV_DEVICE;
eventdev_conf.queue_port_id = 0;

source1 = rte_sampler_eventdev_source_register(session1, dev_id,
       &eventdev_conf);
if (source1 == NULL) {
printf("Failed to register eventdev source to session 1\n");
goto cleanup_sink1;
}

printf("  - Registered eventdev source\n");
printf("  - Registered console sink\n\n");

/* Create Session 2: Slow sampling every 3 seconds, runs indefinitely */
session_conf.sample_interval_ms = 3000;
session_conf.duration_ms = 0;  /* Infinite */
session_conf.name = "slow_session";

session2 = rte_sampler_session_create(&session_conf);
if (session2 == NULL) {
printf("Failed to create session 2\n");
goto cleanup_source1;
}

printf("Session 2 created: %s (interval=3s, duration=infinite)\n",
       session_conf.name);

/* Setup console sink for session 2 */
sink2 = rte_sampler_sink_register(session2, "console_slow",
   &sink_ops, NULL);
if (sink2 == NULL) {
printf("Failed to register sink for session 2\n");
goto cleanup_session2;
}

/* Register eventdev source to session 2 */
source2 = rte_sampler_eventdev_source_register(session2, dev_id,
       &eventdev_conf);
if (source2 == NULL) {
printf("Failed to register eventdev source to session 2\n");
goto cleanup_sink2;
}

printf("  - Registered eventdev source\n");
printf("  - Registered console sink\n\n");

/* Start both sessions */
rte_sampler_session_start(session1);
rte_sampler_session_start(session2);

printf("Both sessions started. Press Ctrl+C to stop...\n");
printf("Session 1 will auto-stop after 10 seconds.\n");
printf("Session 2 will run until you stop it.\n\n");

/* Main polling loop */
while (!force_quit) {
/* Poll all active sessions */
rte_sampler_poll();

/* Small delay to avoid busy loop */
rte_delay_ms(100);

/* Check if session1 has expired */
if (rte_sampler_session_is_active(session1) == 0 &&
    rte_sampler_session_is_active(session2) == 1) {
static int once = 0;
if (!once) {
printf("\n[INFO] Session 1 duration expired (10s reached)\n");
printf("[INFO] Session 2 still running...\n\n");
once = 1;
}
}
}

printf("\nCleaning up...\n");

/* Cleanup session 2 */
rte_sampler_source_unregister(source2);
cleanup_sink2:
rte_sampler_sink_unregister(sink2);
cleanup_session2:
rte_sampler_session_free(session2);

/* Cleanup session 1 */
cleanup_source1:
rte_sampler_source_unregister(source1);
cleanup_sink1:
rte_sampler_sink_unregister(sink1);
cleanup_session1:
rte_sampler_session_free(session1);

rte_eal_cleanup();

printf("Sampler example completed.\n");
return 0;
}
