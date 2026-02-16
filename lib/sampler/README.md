# DPDK Sampler Library

## Overview

The DPDK Sampler library provides a generic framework for sampling extended statistics (xstats) from various DPDK subsystems and outputting them to multiple destinations. It implements a flexible source/sink architecture with support for multiple independent sessions, each with configurable sampling intervals and durations.

## Key Features

- **Multiple Sessions**: Run up to 32 concurrent sampling sessions with independent configurations
- **Configurable Intervals**: Each session can sample at different rates (milliseconds)
- **Duration Control**: Sessions can run for a fixed duration or indefinitely
- **Automatic Polling**: Built-in polling mechanism for periodic sampling
- **Manual Sampling**: Support for on-demand sampling
- **Type-Safe API**: Pointer-based API for compile-time type checking
- **Flexible Sources**: Pluggable statistics sources (eventdev, ethdev, cryptodev, etc.)
- **Multiple Sinks**: Pluggable output destinations (console, metrics, telemetry, file, etc.)
- **Standard xstats API**: Compatible with existing DPDK xstats patterns

## Architecture

### Sessions
Sessions represent independent sampling contexts with their own:
- Sampling interval (how often to sample)
- Duration (how long to run)
- Set of sources (what to sample from)
- Set of sinks (where to output)

### Sources
Sources represent providers of statistics. Each source implements callbacks to:
- Get xstats names and IDs
- Get xstats values
- Reset xstats (optional)

Currently supported sources:
- **Eventdev**: Sample device, port, or queue level xstats from eventdev

### Sinks
Sinks represent output destinations for sampled statistics. Each sink implements a callback to receive and process sampled data.

Future sink implementations could include:
- Metrics library integration
- Telemetry integration
- File/CSV output
- Database storage
- Network streaming

## API Overview

### Session Management

```c
/* Create a session */
struct rte_sampler_session_conf conf = {
    .sample_interval_ms = 1000,  /* Sample every 1 second */
    .duration_ms = 60000,        /* Run for 60 seconds (0 = infinite) */
    .name = "my_session",
};
struct rte_sampler_session *session = rte_sampler_session_create(&conf);

/* Start session */
rte_sampler_session_start(session);

/* Check if session is active */
int active = rte_sampler_session_is_active(session);

/* Stop session */
rte_sampler_session_stop(session);

/* Free session */
rte_sampler_session_free(session);
```

### Source Registration

```c
/* Register a source - returns pointer */
struct rte_sampler_source *source = rte_sampler_source_register(
    session,
    "source_name",
    source_id,
    &ops,
    user_data
);

/* Unregister source */
rte_sampler_source_unregister(source);
```

### Sink Registration

```c
/* Register a sink - returns pointer */
struct rte_sampler_sink *sink = rte_sampler_sink_register(
    session,
    "sink_name",
    &ops,
    user_data
);

/* Unregister sink */
rte_sampler_sink_unregister(sink);
```

### Sampling

```c
/* Manual sampling (any session) */
rte_sampler_sample(session);

/* Automatic polling (all active sessions with interval > 0) */
while (running) {
    rte_sampler_poll();  /* Call from main loop */
    rte_delay_ms(100);
}
```

### Standard xstats API

```c
/* Get xstats names */
int count = rte_sampler_xstats_names_get(session, source, names, size);

/* Get xstats values */
int count = rte_sampler_xstats_get(session, source, ids, values, n);

/* Reset xstats */
rte_sampler_xstats_reset(session, source, ids, n);
```

### Filtering and Capacity Management

```c
/* Set filter to sample only specific statistics */
const char *patterns[] = { "rx_pkts*", "tx_pkts*" };
rte_sampler_source_set_filter(source, patterns, 2);

/* Get the number of xstats after filter is applied */
int count = rte_sampler_source_get_xstats_count(source);
/* Use this count to allocate appropriately sized buffers */

/* Clear filter to sample all statistics */
rte_sampler_source_clear_filter(source);

/* Get filter patterns */
char *filter_patterns[10];
int num_patterns = rte_sampler_source_get_filter(source, filter_patterns, 10);
```

## Usage Examples

### Example 1: Single Session with Manual Sampling

```c
#include <rte_sampler.h>
#include <rte_sampler_eventdev.h>

/* Create manual session (interval = 0) */
struct rte_sampler_session_conf conf = {
    .sample_interval_ms = 0,     /* Manual sampling */
    .duration_ms = 0,            /* Infinite */
    .name = "manual_session",
};
struct rte_sampler_session *session = rte_sampler_session_create(&conf);

/* Register eventdev source */
struct rte_sampler_eventdev_conf eventdev_conf = {
    .mode = RTE_SAMPLER_EVENTDEV_DEVICE,
    .queue_port_id = 0,
};
struct rte_sampler_source *source = 
    rte_sampler_eventdev_source_register(session, dev_id, &eventdev_conf);

/* Register console sink */
struct rte_sampler_sink_ops sink_ops = {
    .output = my_console_output,
};
struct rte_sampler_sink *sink = 
    rte_sampler_sink_register(session, "console", &sink_ops, NULL);

/* Start session */
rte_sampler_session_start(session);

/* Manual sampling in application loop */
while (running) {
    rte_sampler_sample(session);
    rte_delay_ms(1000);
}

/* Cleanup */
rte_sampler_source_unregister(source);
rte_sampler_sink_unregister(sink);
rte_sampler_session_free(session);
```

### Example 2: Multiple Sessions with Automatic Polling

```c
/* Create fast session (1 second interval, 60 second duration) */
struct rte_sampler_session_conf fast_conf = {
    .sample_interval_ms = 1000,
    .duration_ms = 60000,
    .name = "fast_session",
};
struct rte_sampler_session *fast_session = 
    rte_sampler_session_create(&fast_conf);

/* Create slow session (5 second interval, infinite duration) */
struct rte_sampler_session_conf slow_conf = {
    .sample_interval_ms = 5000,
    .duration_ms = 0,
    .name = "slow_session",
};
struct rte_sampler_session *slow_session = 
    rte_sampler_session_create(&slow_conf);

/* Register sources and sinks to both sessions ... */

/* Start both sessions */
rte_sampler_session_start(fast_session);
rte_sampler_session_start(slow_session);

/* Automatic polling in main loop */
while (running) {
    rte_sampler_poll();  /* Samples both sessions at their intervals */
    rte_delay_ms(100);
    
    /* Fast session will auto-expire after 60 seconds */
    /* Slow session will continue indefinitely */
}
```

### Example 3: Sink Implementation

```c
static int
my_sink_output(const char *source_name,
               uint16_t source_id,
               const struct rte_sampler_xstats_name *xstats_names,
               const uint64_t *ids,
               const uint64_t *values,
               unsigned int n,
               void *user_data)
{
    FILE *fp = (FILE *)user_data;
    
    fprintf(fp, "Source: %s (ID=%u)\n", source_name, source_id);
    for (unsigned int i = 0; i < n; i++) {
        fprintf(fp, "  %s = %lu\n", xstats_names[i].name, values[i]);
    }
    fflush(fp);
    
    return 0;
}

/* Use the sink */
FILE *fp = fopen("stats.txt", "w");
struct rte_sampler_sink_ops ops = { .output = my_sink_output };
struct rte_sampler_sink *sink = 
    rte_sampler_sink_register(session, "file_sink", &ops, fp);
```

## Implementation Limits

- Maximum sessions: 32
- Maximum sources per session: 64
- Maximum sinks per session: 16
- Maximum xstats per source: 256

These limits can be adjusted by modifying the constants in `rte_sampler.c`.

## Session Lifecycle

1. **Create** → Session is created but inactive
2. **Start** → Session becomes active, polling begins (if interval > 0)
3. **Running** → Session samples according to its interval
4. **Expired** → Session duration reached, automatically stops
5. **Stopped** → Manually stopped via `rte_sampler_session_stop()`
6. **Free** → Session resources are released

## Future Enhancements

1. **Additional Sources**:
   - Ethdev xstats sampler
   - Cryptodev xstats sampler
   - Rawdev xstats sampler
   - Custom application metrics

2. **Additional Sinks**:
   - Metrics library integration
   - Telemetry integration
   - File/CSV output with rotation
   - InfluxDB/Prometheus exporters
   - JSON-RPC interface

3. **Features**:
   - Per-source sampling intervals
   - Statistics filtering and aggregation
   - Delta mode (sample differences)
   - Threshold-based sampling
   - Compression and batching
   - Session priorities

## License

SPDX-License-Identifier: BSD-3-Clause
Copyright(c) 2024 Intel Corporation
