<!--
  SPDX-License-Identifier: BSD-3-Clause
  Copyright(c) 2024 Intel Corporation
-->

# Sampler Library Performance Optimization

## Problem: Large Name Array Overhead

By default, every time a source is sampled, the sink callback receives:
- Array of stat **names** (up to 256 entries × 128 bytes = **32KB**)
- Array of stat IDs (up to 256 entries × 8 bytes = 2KB)
- Array of stat values (up to 256 entries × 8 bytes = 2KB)

**Total: ~36KB per sample**

For high-frequency sampling (e.g., every 100ms), this creates significant memory bandwidth overhead, especially when:
- The sink already knows the stat names
- The sink only cares about IDs and values
- Multiple sources are being sampled

## Solution: RTE_SAMPLER_SINK_F_NO_NAMES Flag

The library provides an optimization flag that allows sinks to opt-out of receiving stat names on every sample.

### Usage

```c
/* Create optimized sink ops */
struct rte_sampler_sink_ops ops;
ops.output = my_sink_callback;
ops.flags = RTE_SAMPLER_SINK_F_NO_NAMES;  /* Don't send names */

/* Register the sink */
struct rte_sampler_sink *sink = rte_sampler_sink_register(
    session,
    "optimized_sink",
    &ops,
    user_data);
```

### Sink Callback Changes

When using `RTE_SAMPLER_SINK_F_NO_NAMES`, the `xstats_names` parameter will be **NULL**:

```c
static int
my_sink_callback(const char *source_name,
                 uint16_t source_id,
                 const struct rte_sampler_xstats_name *xstats_names,  /* Will be NULL */
                 const uint64_t *ids,      /* Always provided */
                 const uint64_t *values,   /* Always provided */
                 unsigned int n,
                 void *user_data)
{
    /* Check if names are provided */
    if (xstats_names == NULL) {
        /* Optimized mode - work with IDs only */
        for (unsigned int i = 0; i < n; i++) {
            printf("ID[%lu] = %lu\n", ids[i], values[i]);
        }
    } else {
        /* Regular mode - names are available */
        for (unsigned int i = 0; i < n; i++) {
            printf("%s = %lu\n", xstats_names[i].name, values[i]);
        }
    }
    
    return 0;
}
```

### On-Demand Name Lookup

If your sink occasionally needs a stat name, you can look it up on-demand:

```c
#include <rte_sampler.h>

/* In your sink callback */
struct rte_sampler_xstats_name name;
int ret = rte_sampler_source_get_xstats_name(source, id, &name);
if (ret == 0) {
    printf("ID %lu is named: %s\n", id, name.name);
}
```

**Note:** You need to cache the source pointer to use this API. Pass it via `user_data`:

```c
struct my_sink_data {
    struct rte_sampler_source *source;  /* Cached source */
};

/* When registering */
my_data->source = source;
sink = rte_sampler_sink_register(session, "sink", &ops, my_data);

/* In callback */
rte_sampler_source_get_xstats_name(data->source, id, &name);
```

## Performance Comparison

### Regular Sink (with names)
```
Per-sample data transfer: ~36KB
- Names array: 32KB
- IDs array: 2KB
- Values array: 2KB
```

### Optimized Sink (without names)
```
Per-sample data transfer: ~4KB
- Names array: 0KB (NULL pointer)
- IDs array: 2KB
- Values array: 2KB

Bandwidth savings: 89% (32KB/36KB)
```

### When to Use Each Mode

**Use Regular Mode (with names) when:**
- Sink is simple and doesn't need optimization
- Names are essential for every sample
- Sampling frequency is low (e.g., once per second)

**Use Optimized Mode (without names) when:**
- High-frequency sampling (e.g., every 100ms)
- Sink already cached the names
- Sink only processes IDs/values (e.g., storing to database by ID)
- Multiple sources with many stats (256+)

## Complete Example

See `examples/sampler/optimized_sink_example.c` for a complete working example that demonstrates both modes.

## Pattern: One-Time Name Cache

A common pattern is to cache names on the first sample:

```c
struct my_sink_data {
    struct rte_sampler_xstats_name *cached_names;
    uint64_t *cached_ids;
    unsigned int num_stats;
    bool cached;
};

static int
my_sink_callback(..., const struct rte_sampler_xstats_name *xstats_names, ...)
{
    struct my_sink_data *data = user_data;
    
    /* Even in optimized mode, you could manually cache on first call */
    if (!data->cached && data->source != NULL) {
        /* Allocate and cache names once */
        data->num_stats = n;
        data->cached_names = malloc(sizeof(*data->cached_names) * n);
        data->cached_ids = malloc(sizeof(*data->cached_ids) * n);
        
        for (unsigned int i = 0; i < n; i++) {
            data->cached_ids[i] = ids[i];
            rte_sampler_source_get_xstats_name(data->source, ids[i],
                                               &data->cached_names[i]);
        }
        data->cached = true;
    }
    
    /* Now use cached names */
    for (unsigned int i = 0; i < n; i++) {
        printf("%s = %lu\n", data->cached_names[i].name, values[i]);
    }
    
    return 0;
}
```

## API Reference

### Flags

- `RTE_SAMPLER_SINK_F_NO_NAMES` - Don't pass stat names to sink

### Functions

```c
/**
 * Get xstats name for a specific source
 * Helper for sinks that use RTE_SAMPLER_SINK_F_NO_NAMES flag
 */
int rte_sampler_source_get_xstats_name(struct rte_sampler_source *source,
                                       uint64_t id,
                                       struct rte_sampler_xstats_name *name);
```

**Returns:**
- 0 on success
- -EINVAL if parameters are invalid
- -ENOENT if ID not found in source

## Benefits

1. **Reduced Memory Bandwidth**: 89% less data per sample
2. **Better Cache Utilization**: Only essential data in hot path
3. **Scalability**: Handle more sources/stats without bottleneck
4. **Flexibility**: On-demand lookup when names are actually needed
5. **Backward Compatible**: Existing sinks continue to work

## Migration Guide

### Migrating Existing Sinks

**Before:**
```c
struct rte_sampler_sink_ops ops = {
    .output = my_callback,
};
```

**After (if you don't need names every time):**
```c
struct rte_sampler_sink_ops ops = {
    .output = my_callback,
    .flags = RTE_SAMPLER_SINK_F_NO_NAMES,  /* Add this */
};

/* Update callback to handle NULL xstats_names */
static int my_callback(..., const struct rte_sampler_xstats_name *xstats_names, ...)
{
    if (xstats_names == NULL) {
        /* Use IDs instead of names */
    }
}
```

## Conclusion

The `RTE_SAMPLER_SINK_F_NO_NAMES` optimization eliminates the need to pass large stat name arrays on every sample, significantly reducing memory bandwidth overhead for high-frequency sampling scenarios.
