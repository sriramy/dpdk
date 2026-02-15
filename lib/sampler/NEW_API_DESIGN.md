<!--
  SPDX-License-Identifier: BSD-3-Clause
  Copyright(c) 2024 Intel Corporation
-->

# New Sampler API Design

## Overview

The sampler library has been redesigned with lifecycle-based operations for sources and sinks, providing better control and batching support.

## Key Changes

### Source Operations (New)

```c
struct rte_sampler_source_ops {
    rte_sampler_source_start start;      // Initialize source
    rte_sampler_source_collect collect;  // Collect samples
    rte_sampler_source_stop stop;        // Stop source
};
```

**Lifecycle:** start → collect (repeatedly) → stop

### Sink Operations (New)

```c
struct rte_sampler_sink_ops {
    rte_sampler_sink_start start;           // Initialize sink
    rte_sampler_sink_report_begin begin;    // Begin report batch
    rte_sampler_sink_report_append append;  // Append data to report
    rte_sampler_sink_report_end end;        // End report batch
    rte_sampler_sink_stop stop;             // Stop sink
    uint32_t flags;                         // Sink flags
};
```

**Lifecycle:** start → (begin → append × N → end) × M → stop

## Sample Data Structure

```c
struct rte_sampler_sample {
    uint64_t timestamp;     // Sample timestamp
    char name[128];         // Stat name
    uint64_t id;           // Stat ID
    uint64_t value;        // Stat value
};
```

## Source Implementation Pattern

```c
static int my_source_start(uint16_t source_id, void *user_data)
{
    // Initialize source
    return 0;
}

static int my_source_collect(uint16_t source_id,
                             struct rte_sampler_sample *samples,
                             unsigned int max_samples,
                             void *user_data)
{
    // Fill samples array
    unsigned int count = 0;
    
    samples[count].timestamp = rte_get_timer_cycles();
    rte_strscpy(samples[count].name, "packets_rx", sizeof(samples[count].name));
    samples[count].id = 0;
    samples[count].value = get_rx_count();
    count++;
    
    return count;  // Return number of samples collected
}

static int my_source_stop(uint16_t source_id, void *user_data)
{
    // Cleanup source
    return 0;
}

// Register source
struct rte_sampler_source_ops ops = {
    .start = my_source_start,
    .collect = my_source_collect,
    .stop = my_source_stop,
};
```

## Sink Implementation Pattern

```c
static int my_sink_start(void *user_data)
{
    // Initialize sink
    return 0;
}

static int my_sink_report_begin(const char *source_name,
                                uint16_t source_id,
                                unsigned int num_samples,
                                void *user_data)
{
    // Prepare for batch of samples
    printf("=== Report from %s (ID=%u): %u samples ===\n",
           source_name, source_id, num_samples);
    return 0;
}

static int my_sink_report_append(const struct rte_sampler_sample *sample,
                                 void *user_data)
{
    // Process one sample
    printf("  %s = %lu\n", sample->name, sample->value);
    return 0;
}

static int my_sink_report_end(void *user_data)
{
    // Finalize batch
    printf("=== End of report ===\n");
    return 0;
}

static int my_sink_stop(void *user_data)
{
    // Cleanup sink
    return 0;
}

// Register sink
struct rte_sampler_sink_ops ops = {
    .start = my_sink_start,
    .begin = my_sink_report_begin,
    .append = my_sink_report_append,
    .end = my_sink_report_end,
    .stop = my_sink_stop,
    .flags = 0,
};
```

## Benefits of New API

1. **Lifecycle Management**: Explicit start/stop phases
2. **Batching Support**: begin/append/end pattern for efficient batch processing
3. **Simpler Data Model**: Single sample structure instead of arrays
4. **Better Control**: Sources and sinks have clear initialization/cleanup phases
5. **Flexibility**: Sinks can optimize based on batch boundaries
6. **Cleaner Interface**: One sample at a time vs arrays of data

## Migration Guide

### Old API (xstats-based)
```c
// Old source
ops.xstats_names_get = get_names;
ops.xstats_get = get_values;
ops.xstats_reset = reset;

// Old sink
ops.output = sink_output;  // Receives arrays
```

### New API (lifecycle-based)
```c
// New source
ops.start = source_start;
ops.collect = source_collect;  // Returns samples array
ops.stop = source_stop;

// New sink
ops.start = sink_start;
ops.begin = report_begin;      // Called once per report
ops.append = report_append;    // Called per sample
ops.end = report_end;          // Called once per report
ops.stop = sink_stop;
```

## Example Flow

```
Session Start:
  → source.start() for all sources
  → sink.start() for all sinks

Each Sample Interval:
  For each source:
    samples[] = source.collect()
    
    For each sink:
      sink.begin(source_name, source_id, num_samples)
      for each sample in samples[]:
        sink.append(sample)
      sink.end()

Session Stop:
  → source.stop() for all sources
  → sink.stop() for all sinks
```

## Advantages

- **Batching**: Sinks can optimize batch operations (e.g., database bulk inserts)
- **Resource Management**: Explicit start/stop for proper cleanup
- **Flexibility**: Append can be called as many times as needed
- **Simplicity**: Each callback has a single, clear purpose
- **Performance**: Sinks can prepare/finalize batches efficiently

## Implementation Status

- [x] API defined in header
- [ ] Core sampling logic updated
- [ ] Eventdev source adapted
- [ ] File sink adapted
- [ ] Ring buffer sink adapted
- [ ] CTF sink adapted
- [ ] Examples updated
