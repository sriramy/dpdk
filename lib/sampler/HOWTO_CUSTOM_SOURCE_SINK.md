# How to Register Custom Sources and Sinks with Custom IDs

## Overview

This guide shows you how to create custom sources and sinks for the sampler library, with support for passing custom sampler IDs from source to sink.

## Problem Statement

You want to:
1. Register a custom source (not eventdev)
2. Register a custom sink
3. Store your own custom ID in the source
4. Pass that custom ID to the sink (instead of just the source_id)

## Solution Approaches

### Approach 1: Embed Custom ID in User Data (Recommended)

Both sources and sinks accept a `user_data` pointer that you can use to pass any custom information.

#### Step 1: Define Your Data Structures

```c
/* Source user data - stores your custom sampler ID */
struct my_source_data {
    uint64_t custom_sampler_id;  /* YOUR custom ID */
    void *other_state;           /* Any other source state */
};

/* Sink user data - can store mappings if needed */
struct my_sink_data {
    FILE *output_file;
    /* Optional: mapping from source_id to sampler_id */
    struct {
        uint16_t source_id;
        uint64_t sampler_id;
    } *id_map;
    unsigned int num_mappings;
};
```

#### Step 2: Implement Source Callbacks

```c
static int
my_source_xstats_names_get(uint16_t source_id,
                           struct rte_sampler_xstats_name *xstats_names,
                           uint64_t *ids,
                           unsigned int size,
                           void *user_data)
{
    struct my_source_data *data = user_data;
    
    /* Return count if xstats_names is NULL */
    if (xstats_names == NULL)
        return 3;  /* Number of stats you provide */
    
    /* Fill in stat names - you can embed your custom_sampler_id here! */
    snprintf(xstats_names[0].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
             "sampler_%lu_packets_tx", data->custom_sampler_id);
    ids[0] = 0;
    
    snprintf(xstats_names[1].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
             "sampler_%lu_packets_rx", data->custom_sampler_id);
    ids[1] = 1;
    
    snprintf(xstats_names[2].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
             "sampler_%lu_errors", data->custom_sampler_id);
    ids[2] = 2;
    
    return 3;
}

static int
my_source_xstats_get(uint16_t source_id,
                     const uint64_t *ids,
                     uint64_t *values,
                     unsigned int n,
                     void *user_data)
{
    struct my_source_data *data = user_data;
    
    /* Get your actual statistics */
    for (unsigned int i = 0; i < n; i++) {
        switch (ids[i]) {
        case 0: values[i] = /* get_packets_tx() */; break;
        case 1: values[i] = /* get_packets_rx() */; break;
        case 2: values[i] = /* get_errors() */; break;
        }
    }
    
    return n;
}

static int
my_source_xstats_reset(uint16_t source_id,
                       const uint64_t *ids,
                       unsigned int n,
                       void *user_data)
{
    /* Optional: reset your statistics */
    return 0;
}
```

#### Step 3: Implement Sink Callback

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
    struct my_sink_data *sink_data = user_data;
    uint64_t sampler_id = 0;
    
    /* Option 1: Extract sampler_id from stat name */
    if (n > 0 && sscanf(xstats_names[0].name, "sampler_%lu_", &sampler_id) == 1) {
        /* Now you have the custom sampler_id! */
    }
    
    /* Option 2: Use your mapping table */
    for (unsigned int i = 0; i < sink_data->num_mappings; i++) {
        if (sink_data->id_map[i].source_id == source_id) {
            sampler_id = sink_data->id_map[i].sampler_id;
            break;
        }
    }
    
    /* Process data with sampler_id */
    fprintf(sink_data->output_file, "Sampler %lu:\n", sampler_id);
    for (unsigned int i = 0; i < n; i++) {
        fprintf(sink_data->output_file, "  %s = %lu\n",
                xstats_names[i].name, values[i]);
    }
    
    return 0;
}
```

#### Step 4: Register Source and Sink

```c
void setup_custom_sampler(struct rte_sampler_session *session)
{
    struct rte_sampler_source_ops source_ops;
    struct rte_sampler_sink_ops sink_ops;
    struct my_source_data *source_data;
    struct my_sink_data *sink_data;
    struct rte_sampler_source *source;
    struct rte_sampler_sink *sink;
    
    /* Allocate and setup source data */
    source_data = malloc(sizeof(*source_data));
    source_data->custom_sampler_id = 12345;  /* YOUR CUSTOM ID */
    
    /* Setup source operations */
    source_ops.xstats_names_get = my_source_xstats_names_get;
    source_ops.xstats_get = my_source_xstats_get;
    source_ops.xstats_reset = my_source_xstats_reset;
    
    /* Register source */
    source = rte_sampler_source_register(
                session,
                "my_custom_source",
                0,              /* source_id (can be any value) */
                &source_ops,
                source_data);   /* user_data contains custom_sampler_id */
    
    if (source == NULL) {
        fprintf(stderr, "Failed to register source\n");
        free(source_data);
        return;
    }
    
    /* Allocate and setup sink data */
    sink_data = malloc(sizeof(*sink_data));
    sink_data->output_file = fopen("output.txt", "w");
    sink_data->num_mappings = 1;
    sink_data->id_map = malloc(sizeof(*sink_data->id_map));
    sink_data->id_map[0].source_id = 0;
    sink_data->id_map[0].sampler_id = source_data->custom_sampler_id;
    
    /* Setup sink operations */
    sink_ops.output = my_sink_output;
    
    /* Register sink */
    sink = rte_sampler_sink_register(
                session,
                "my_custom_sink",
                &sink_ops,
                sink_data);     /* user_data */
    
    if (sink == NULL) {
        fprintf(stderr, "Failed to register sink\n");
        fclose(sink_data->output_file);
        free(sink_data->id_map);
        free(sink_data);
        rte_sampler_source_unregister(source);
        free(source_data);
        return;
    }
    
    printf("Custom source and sink registered successfully!\n");
}
```

### Approach 2: Use Stat Names to Carry IDs

Embed the custom sampler ID directly in the stat names:

```c
/* In xstats_names_get callback */
snprintf(xstats_names[i].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
         "sampler_%lu_%s", custom_sampler_id, stat_base_name);

/* In sink callback */
uint64_t sampler_id;
sscanf(xstats_names[0].name, "sampler_%lu_", &sampler_id);
```

**Pros:**
- No extra data structures needed
- ID is self-contained in the data

**Cons:**
- Parsing required in sink
- Uses up name space

### Approach 3: Shared Context Object

Create a shared context that both source and sink can access:

```c
struct shared_sampler_context {
    uint64_t sampler_id;
    /* Other shared state */
};

/* Pass the same context to both source and sink */
struct shared_sampler_context *ctx = malloc(sizeof(*ctx));
ctx->sampler_id = 12345;

source = rte_sampler_source_register(session, "src", 0, &ops, ctx);
sink = rte_sampler_sink_register(session, "sink", &ops, ctx);
```

## Complete Working Example

See `examples/custom_source_sink.c` for a complete working example with multiple approaches.

## Key Points

1. **user_data is your friend**: Both source and sink registration accept a `void *user_data` parameter that gets passed to all callbacks

2. **Multiple sources, one sink**: You can register multiple sources with different sampler IDs to the same session, and one sink can handle all of them using a mapping table

3. **Stat names can carry metadata**: The stat names are up to 128 characters, so you can embed your custom IDs there

4. **source_id vs sampler_id**: 
   - `source_id` (uint16_t) is the device/instance ID passed to `rte_sampler_source_register()`
   - Your custom `sampler_id` (uint64_t or any type) is stored in your user_data

5. **Cleanup**: Remember to free your user_data when unregistering sources and sinks

## Testing Your Implementation

```c
/* Create session */
struct rte_sampler_session_conf conf = {
    .sample_interval_ms = 1000,
    .duration_ms = 0,
    .name = "test_session",
};
struct rte_sampler_session *session = rte_sampler_session_create(&conf);

/* Register your custom source and sink */
setup_custom_sampler(session);

/* Start and sample */
rte_sampler_session_start(session);
while (active) {
    rte_sampler_sample(session);  /* Manual sampling */
    rte_delay_ms(1000);
}

/* Cleanup */
rte_sampler_source_unregister(source);
rte_sampler_sink_unregister(sink);
rte_sampler_session_free(session);
```

## FAQ

**Q: Can I have multiple sinks for one source?**  
A: Yes! Register multiple sinks to the same session, and all of them will receive the sampled data.

**Q: Can I have multiple sources for one sink?**  
A: Yes! The sink will receive data from all sources in the session. Use the `source_name` and `source_id` parameters to distinguish them.

**Q: How do I pass the sampler_id to the sink?**  
A: Three ways:
1. Embed it in stat names (Approach 2)
2. Use a mapping table in sink user_data (Approach 1)
3. Use a shared context object (Approach 3)

**Q: Can the sampler_id be different from the source_id?**  
A: Yes! The `source_id` is just a uint16_t identifier for the source instance. Your `sampler_id` can be anything (uint64_t, struct, etc.) stored in user_data.
