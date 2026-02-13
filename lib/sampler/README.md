# DPDK Sampler Library

## Overview

The DPDK Sampler library provides a generic framework for sampling extended statistics (xstats) from various DPDK subsystems and outputting them to multiple destinations. It implements a flexible source/sink architecture that allows easy integration with different DPDK components.

## Architecture

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

### Core Functions

```c
/* Create/destroy sampler */
struct rte_sampler *rte_sampler_create(void);
void rte_sampler_free(struct rte_sampler *sampler);

/* Register sources and sinks */
int rte_sampler_source_register(struct rte_sampler *sampler,
                                 const char *source_name,
                                 uint16_t source_id,
                                 const struct rte_sampler_source_ops *ops,
                                 void *user_data);

int rte_sampler_sink_register(struct rte_sampler *sampler,
                               const char *sink_name,
                               const struct rte_sampler_sink_ops *ops,
                               void *user_data);

/* Perform sampling */
int rte_sampler_sample(struct rte_sampler *sampler);

/* Standard xstats API */
int rte_sampler_xstats_names_get(struct rte_sampler *sampler,
                                  int source_handle,
                                  struct rte_sampler_xstats_name *xstats_names,
                                  unsigned int size);

int rte_sampler_xstats_get(struct rte_sampler *sampler,
                            int source_handle,
                            const uint64_t *ids,
                            uint64_t *values,
                            unsigned int n);

int rte_sampler_xstats_reset(struct rte_sampler *sampler,
                              int source_handle,
                              const uint64_t *ids,
                              unsigned int n);
```

### Eventdev Source

```c
/* Register an eventdev as a source */
int rte_sampler_eventdev_source_register(struct rte_sampler *sampler,
                                          uint8_t dev_id,
                                          const struct rte_sampler_eventdev_conf *conf);
```

## Usage Example

### Basic Eventdev Sampling

```c
#include <rte_sampler.h>
#include <rte_sampler_eventdev.h>

/* Create sampler */
struct rte_sampler *sampler = rte_sampler_create();
if (sampler == NULL) {
    /* Handle error */
}

/* Configure eventdev source */
struct rte_sampler_eventdev_conf conf = {
    .mode = RTE_SAMPLER_EVENTDEV_DEVICE,
    .queue_port_id = 0,
};

/* Register eventdev as source */
int source_handle = rte_sampler_eventdev_source_register(sampler, dev_id, &conf);
if (source_handle < 0) {
    /* Handle error */
}

/* Implement and register a sink */
static int my_sink_output(const char *source_name,
                          uint16_t source_id,
                          const struct rte_sampler_xstats_name *xstats_names,
                          const uint64_t *ids,
                          const uint64_t *values,
                          unsigned int n,
                          void *user_data)
{
    for (unsigned int i = 0; i < n; i++) {
        printf("%s[%u].%s = %lu\n",
               source_name, source_id,
               xstats_names[i].name, values[i]);
    }
    return 0;
}

struct rte_sampler_sink_ops sink_ops = {
    .output = my_sink_output,
};

int sink_handle = rte_sampler_sink_register(sampler, "console", &sink_ops, NULL);
if (sink_handle < 0) {
    /* Handle error */
}

/* Periodic sampling loop */
while (running) {
    rte_sampler_sample(sampler);
    rte_delay_ms(1000);  /* Sample every second */
}

/* Cleanup */
rte_sampler_source_unregister(sampler, source_handle);
rte_sampler_sink_unregister(sampler, sink_handle);
rte_sampler_free(sampler);
```

## Implementation Limits

- Maximum sources: 64
- Maximum sinks: 16
- Maximum xstats per source: 256

These limits are defined in the implementation and can be adjusted if needed.

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
   - Configurable sampling intervals per source
   - Statistics filtering and aggregation
   - Delta mode (sample differences)
   - Threshold-based sampling
   - Compression and batching

## License

SPDX-License-Identifier: BSD-3-Clause
Copyright(c) 2024 Intel Corporation
