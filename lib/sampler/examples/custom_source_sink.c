/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

/**
 * Example: Creating Custom Source and Sink with Custom IDs
 * 
 * This example shows how to:
 * 1. Create a custom source that provides xstats
 * 2. Create a custom sink that receives sampled data
 * 3. Pass custom sampler IDs to the sink using user_data
 */

#include <stdio.h>
#include <rte_sampler.h>

/**
 * Custom source user data structure
 * Store any source-specific state here
 */
struct custom_source_data {
	uint64_t custom_sampler_id;  /**< Your custom ID to pass to sink */
	void *source_specific_state;  /**< Any other source state */
};

/**
 * Custom sink user data structure
 * Store any sink-specific state here
 */
struct custom_sink_data {
	FILE *output_file;
	uint64_t sink_instance_id;
};

/**
 * Custom source: xstats_names_get callback
 * 
 * This is called to get the list of available statistics from your source.
 * The source_id is the ID you passed during registration.
 */
static int
		custom_source_xstats_names_get(uint16_t source_id,
struct rte_sampler_xstats_name *xstats_names,
uint64_t *ids,
unsigned int size,
void *user_data)
{
struct custom_source_data *data = user_data;
unsigned int i;

/* Example: Return 5 statistics */
const int num_stats = 5;

/* If xstats_names is NULL, just return the count */
if (xstats_names == NULL)
return num_stats;

/* Fill in stat names and IDs */
for (i = 0; i < num_stats && i < size; i++) {
snprintf(xstats_names[i].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
 "custom_stat_%u_sampler_%lu", i, data->custom_sampler_id);
ids[i] = i;  /* These are the stat IDs from your source */
}

return num_stats;
}

/**
 * Custom source: xstats_get callback
 * 
 * This is called to get the actual values of the statistics.
 */
static int
		custom_source_xstats_get(uint16_t source_id,
		const uint64_t *ids,
		uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct custom_source_data *data = user_data;
unsigned int i;

/* Fill in the values for the requested IDs */
for (i = 0; i < n; i++) {
/* Example: Generate some sample values */
values[i] = (data->custom_sampler_id * 1000) + ids[i] * 10;
}

return n;
}

/**
 * Custom source: xstats_reset callback (optional)
 */
static int
		custom_source_xstats_reset(uint16_t source_id,
		const uint64_t *ids,
		unsigned int n,
		void *user_data)
{
/* Reset your statistics if needed */
return 0;
}

/**
 * Custom sink: output callback
 * 
 * This receives the sampled statistics.
 * You can access your custom sampler ID through user_data.
 */
static int
		custom_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct custom_sink_data *sink_data = user_data;
unsigned int i;

/* Write to file or process the data */
fprintf(sink_data->output_file, 
"=== Sink Instance %lu: Source %s (ID=%u) ===\n",
sink_data->sink_instance_id, source_name, source_id);

for (i = 0; i < n; i++) {
fprintf(sink_data->output_file, "  [%lu] %s = %lu\n",
ids[i], xstats_names[i].name, values[i]);
}

fflush(sink_data->output_file);
return 0;
}

/**
 * Example usage: Registering custom source and sink
 */
void example_register_custom_source_sink(struct rte_sampler_session *session)
{
struct rte_sampler_source_ops source_ops;
struct rte_sampler_sink_ops sink_ops;
struct custom_source_data *source_data;
struct custom_sink_data *sink_data;
struct rte_sampler_source *source;
struct rte_sampler_sink *sink;

/* 
 * STEP 1: Create and register custom source
 */

/* Allocate source user data */
source_data = malloc(sizeof(*source_data));
source_data->custom_sampler_id = 12345;  /* Your custom ID */
source_data->source_specific_state = NULL;

/* Setup source operations */
source_ops.xstats_names_get = custom_source_xstats_names_get;
source_ops.xstats_get = custom_source_xstats_get;
source_ops.xstats_reset = custom_source_xstats_reset;

/* Register the source */
source = rte_sampler_session_register_source(
session,
"my_custom_source",  /* Source name */
0,                    /* source_id (device/instance ID) */
&source_ops,
source_data);         /* user_data - contains custom_sampler_id */

if (source == NULL) {
fprintf(stderr, "Failed to register custom source\n");
free(source_data);
return;
}

/*
 * STEP 2: Create and register custom sink
 */

/* Allocate sink user data */
sink_data = malloc(sizeof(*sink_data));
sink_data->output_file = fopen("sampler_output.txt", "w");
sink_data->sink_instance_id = 99;  /* Your sink instance ID */

/* Setup sink operations */
sink_ops.output = custom_sink_output;

/* Register the sink */
sink = rte_sampler_session_register_sink(
session,
"my_custom_sink",     /* Sink name */
&sink_ops,
sink_data);           /* user_data */

if (sink == NULL) {
fprintf(stderr, "Failed to register custom sink\n");
fclose(sink_data->output_file);
free(sink_data);
rte_sampler_session_unregister_source(session, source);
free(source_data);
return;
}

/*
 * STEP 3: Start sampling
 * The custom_sampler_id from source_data is now embedded in the stat names
 * and the sink can access its own context through sink_data
 */

printf("Custom source and sink registered successfully!\n");
printf("Source has custom_sampler_id: %lu\n", source_data->custom_sampler_id);
printf("Sink has instance_id: %lu\n", sink_data->sink_instance_id);
}

/**
 * Alternative approach: Pass sampler ID through stat names
 * 
 * If you need the sink to know which sampler ID a stat came from,
 * you can encode it in the stat name as shown above, or use a mapping structure.
 */

/**
 * Advanced: Using a mapping structure to associate source IDs with sampler IDs
 */
struct sampler_id_map {
	uint16_t source_id;
	uint64_t sampler_id;
};

struct advanced_sink_data {
	FILE *output_file;
	struct sampler_id_map *id_map;
	unsigned int num_mappings;
};

static int
		advanced_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
struct advanced_sink_data *sink_data = user_data;
uint64_t sampler_id = 0;
unsigned int i;

/* Look up the sampler ID for this source_id */
for (i = 0; i < sink_data->num_mappings; i++) {
if (sink_data->id_map[i].source_id == source_id) {
sampler_id = sink_data->id_map[i].sampler_id;
break;
}
}

/* Now write with the sampler_id instead of source_id */
fprintf(sink_data->output_file, 
"=== Sampler ID %lu (Source %s, ID=%u) ===\n",
sampler_id, source_name, source_id);

for (i = 0; i < n; i++) {
fprintf(sink_data->output_file, "  [%lu] %s = %lu\n",
ids[i], xstats_names[i].name, values[i]);
}

fflush(sink_data->output_file);
return 0;
}

/**
 * Register with ID mapping
 */
void example_register_with_id_mapping(struct rte_sampler_session *session)
{
struct rte_sampler_source_ops source_ops;
struct rte_sampler_sink_ops sink_ops;
struct custom_source_data *source_data1, *source_data2;
struct advanced_sink_data *sink_data;
struct rte_sampler_source *source1, *source2;
struct rte_sampler_sink *sink;

/* Create two sources with different sampler IDs */
source_data1 = malloc(sizeof(*source_data1));
source_data1->custom_sampler_id = 100;
source_ops.xstats_names_get = custom_source_xstats_names_get;
source_ops.xstats_get = custom_source_xstats_get;
source_ops.xstats_reset = custom_source_xstats_reset;
source1 = rte_sampler_session_register_source(session, "source1", 0, 
      &source_ops, source_data1);

source_data2 = malloc(sizeof(*source_data2));
source_data2->custom_sampler_id = 200;
source2 = rte_sampler_session_register_source(session, "source2", 1, 
      &source_ops, source_data2);

/* Create sink with ID mapping */
sink_data = malloc(sizeof(*sink_data));
sink_data->output_file = fopen("advanced_output.txt", "w");
sink_data->num_mappings = 2;
sink_data->id_map = malloc(sizeof(struct sampler_id_map) * 2);
sink_data->id_map[0].source_id = 0;
sink_data->id_map[0].sampler_id = source_data1->custom_sampler_id;
sink_data->id_map[1].source_id = 1;
sink_data->id_map[1].sampler_id = source_data2->custom_sampler_id;

sink_ops.output = advanced_sink_output;
sink = rte_sampler_session_register_sink(session, "advanced_sink",
  &sink_ops, sink_data);

printf("Advanced setup complete!\n");
printf("Source 1 (source_id=0) has sampler_id=%lu\n", 
       source_data1->custom_sampler_id);
printf("Source 2 (source_id=1) has sampler_id=%lu\n", 
       source_data2->custom_sampler_id);
}
