<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- Copyright(c) 2024 Intel Corporation -->

# Solution: Avoiding Large Name Array Transfers

## Your Requirement

> "I wanted to solve the issue of having to pass a large number of name strings around from source to the sink"

## Problem

Every sample transferred **32KB of stat names** (256 names × 128 bytes) even though:
- Names rarely change
- Sinks often don't need them every time
- This creates memory bandwidth bottleneck

## Solution Implemented

### RTE_SAMPLER_SINK_F_NO_NAMES Flag

Sinks can now opt-out of receiving names:

```c
/* Old way - receives 32KB of names every sample */
struct rte_sampler_sink_ops ops = {
    .output = my_callback,
};

/* New way - receives only IDs (no names!) */
struct rte_sampler_sink_ops ops = {
    .output = my_callback,
    .flags = RTE_SAMPLER_SINK_F_NO_NAMES,  /* NEW! */
};
```

### In Your Sink Callback

```c
static int my_sink(const char *source_name,
                   uint16_t source_id,
                   const struct rte_sampler_xstats_name *xstats_names,  /* NULL! */
                   const uint64_t *ids,      /* Compact IDs */
                   const uint64_t *values,
                   unsigned int n,
                   void *user_data)
{
    /* xstats_names is NULL - no 32KB overhead! */
    /* Work with compact IDs instead */
    for (unsigned int i = 0; i < n; i++) {
        process_stat(ids[i], values[i]);  /* Only 16 bytes per stat */
    }
    
    return 0;
}
```

### When You Need a Name

On-demand lookup (rare case):

```c
struct rte_sampler_xstats_name name;
rte_sampler_source_get_xstats_name(source, id, &name);
printf("ID %lu is: %s\n", id, name.name);
```

## Performance Impact

| Mode | Data Per Sample | Bandwidth |
|------|----------------|-----------|
| **Old (with names)** | 32KB names + 2KB IDs + 2KB values = 36KB | 100% |
| **New (without names)** | 0KB names + 2KB IDs + 2KB values = 4KB | **11%** |
| **Savings** | 32KB saved | **89% reduction** |

### Real-World Example

**Scenario:** 10 sources, each with 200 stats, sampled every 100ms

**Before:**
- Per sample: 10 sources × 200 stats × 128 bytes = 256KB names
- Per second: 256KB × 10 samples = 2.56 MB/s bandwidth

**After (with optimization):**
- Per sample: 0KB names (just IDs)
- Per second: 0KB names
- **Saved: 2.56 MB/s bandwidth**

## Files Changed

1. `lib/sampler/rte_sampler.h` - Added flag and helper API
2. `lib/sampler/rte_sampler.c` - Conditional name passing
3. `lib/sampler/PERFORMANCE_OPTIMIZATION.md` - Full documentation
4. `examples/sampler/optimized_sink_example.c` - Working demo

## How to Use

### Step 1: Set the Flag

```c
struct rte_sampler_sink_ops ops;
ops.output = my_sink_callback;
ops.flags = RTE_SAMPLER_SINK_F_NO_NAMES;  /* Don't send names */
```

### Step 2: Handle NULL in Callback

```c
static int my_sink_callback(..., const struct rte_sampler_xstats_name *xstats_names, ...)
{
    /* xstats_names will be NULL */
    assert(xstats_names == NULL);
    
    /* Use IDs instead of names */
    store_to_database(ids, values, n);
    return 0;
}
```

### Step 3: Optional Name Lookup

```c
/* Cache source pointer in your user_data */
struct my_sink_data {
    struct rte_sampler_source *source;
};

/* In callback, lookup when needed */
if (need_name) {
    struct rte_sampler_xstats_name name;
    rte_sampler_source_get_xstats_name(data->source, id, &name);
}
```

## When to Use This Optimization

✅ **Use optimization when:**
- High-frequency sampling (>10 samples/sec)
- Large number of stats (>100 per source)
- Sink stores data by ID (database, time-series)
- Sink already cached names

❌ **Don't use when:**
- Low-frequency sampling (<1 sample/sec)
- Sink needs names every time (logging, debugging)
- Simplicity is more important than performance

## Backward Compatibility

- ✅ No breaking changes
- ✅ Existing sinks work unchanged
- ✅ New sinks can opt-in to optimization
- ✅ Mix optimized and non-optimized sinks in same session

## Summary

Your issue is **solved**! Instead of passing 32KB of name strings on every sample, optimized sinks receive only compact IDs (2KB), reducing bandwidth by 89%.

The solution:
1. Uses a flag to opt-in
2. Passes NULL for names when optimization enabled
3. Provides on-demand lookup when names are actually needed
4. Fully backward compatible
