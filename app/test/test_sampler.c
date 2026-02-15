/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_sampler.h>

#include "test.h"

#define TEST_NUM_STATS 100
#define OLD_MAX_SOURCES 64
#define OLD_MAX_SESSIONS 32

/* Test source callbacks */
static int
test_xstats_names_get(uint16_t source_id,
		struct rte_sampler_xstats_name *xstats_names,
		uint64_t *ids,
		unsigned int size,
		void *user_data)
{
	unsigned int i;
	int *num_stats = (int *)user_data;

	RTE_SET_USED(source_id);

	/* If querying size */
	if (xstats_names == NULL)
		return *num_stats;

	/* Fill in stats */
	for (i = 0; i < (unsigned int)*num_stats && i < size; i++) {
		snprintf(xstats_names[i].name, RTE_SAMPLER_XSTATS_NAME_SIZE,
			"test_stat_%u", i);
		ids[i] = i;
	}

	return *num_stats;
}

static int
test_xstats_get(uint16_t source_id,
		const uint64_t *ids,
		uint64_t *values,
		unsigned int n,
		void *user_data)
{
	unsigned int i;

	RTE_SET_USED(source_id);
	RTE_SET_USED(user_data);

	for (i = 0; i < n; i++)
		values[i] = ids[i] * 100;

	return n;
}

/* Test sink callback */
static int
test_sink_output(const char *source_name,
		uint16_t source_id,
		const struct rte_sampler_xstats_name *xstats_names,
		const uint64_t *ids,
		const uint64_t *values,
		unsigned int n,
		void *user_data)
{
	unsigned int *count = (unsigned int *)user_data;

	RTE_SET_USED(source_name);
	RTE_SET_USED(source_id);
	RTE_SET_USED(xstats_names);
	RTE_SET_USED(ids);
	RTE_SET_USED(values);
	RTE_SET_USED(n);

	(*count)++;
	return 0;
}

/* Test basic session creation and deletion */
static int
test_sampler_session_create_free(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 1000;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

/* Test session start and stop */
static int
test_sampler_session_start_stop(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	int ret;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	ret = rte_sampler_session_start(session);
	if (ret != 0) {
		printf("Failed to start session\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	ret = rte_sampler_session_is_active(session);
	if (ret != 1) {
		printf("Session should be active\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	ret = rte_sampler_session_stop(session);
	if (ret != 0) {
		printf("Failed to stop session\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	ret = rte_sampler_session_is_active(session);
	if (ret != 0) {
		printf("Session should be inactive\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

/* Test source registration and unregistration */
static int
test_sampler_source_register(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	struct rte_sampler_source *source;
	struct rte_sampler_source_ops ops;
	int num_stats = TEST_NUM_STATS;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	memset(&ops, 0, sizeof(ops));
	ops.xstats_names_get = test_xstats_names_get;
	ops.xstats_get = test_xstats_get;

	source = rte_sampler_source_register(session, "test_source", 0, &ops, &num_stats);
	if (source == NULL) {
		printf("Failed to register source\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_source_unregister(source);
	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

/* Test sink registration and unregistration */
static int
test_sampler_sink_register(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	struct rte_sampler_sink *sink;
	struct rte_sampler_sink_ops ops;
	unsigned int sink_count = 0;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	memset(&ops, 0, sizeof(ops));
	ops.output = test_sink_output;

	sink = rte_sampler_sink_register(session, "test_sink", &ops, &sink_count);
	if (sink == NULL) {
		printf("Failed to register sink\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_sink_unregister(sink);
	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

/* Test sampling with one source and one sink */
static int
test_sampler_sample_basic(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	struct rte_sampler_source *source;
	struct rte_sampler_sink *sink;
	struct rte_sampler_source_ops src_ops;
	struct rte_sampler_sink_ops sink_ops;
	int num_stats = TEST_NUM_STATS;
	unsigned int sink_count = 0;
	int ret;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	memset(&src_ops, 0, sizeof(src_ops));
	src_ops.xstats_names_get = test_xstats_names_get;
	src_ops.xstats_get = test_xstats_get;

	source = rte_sampler_source_register(session, "test_source", 0, &src_ops, &num_stats);
	if (source == NULL) {
		printf("Failed to register source\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	memset(&sink_ops, 0, sizeof(sink_ops));
	sink_ops.output = test_sink_output;

	sink = rte_sampler_sink_register(session, "test_sink", &sink_ops, &sink_count);
	if (sink == NULL) {
		printf("Failed to register sink\n");
		rte_sampler_source_unregister(source);
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_session_start(session);
	ret = rte_sampler_sample(session);
	if (ret != 0) {
		printf("Sampling failed\n");
		rte_sampler_sink_unregister(sink);
		rte_sampler_source_unregister(source);
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	if (sink_count != 1) {
		printf("Expected 1 sample, got %u\n", sink_count);
		rte_sampler_sink_unregister(sink);
		rte_sampler_source_unregister(source);
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_session_stop(session);
	rte_sampler_sink_unregister(sink);
	rte_sampler_source_unregister(source);
	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

/* Test dynamic allocation beyond old static limits */
static int
test_sampler_dynamic_sources(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	struct rte_sampler_source **sources;
	struct rte_sampler_sink *sink;
	struct rte_sampler_source_ops src_ops;
	struct rte_sampler_sink_ops sink_ops;
	int num_stats = TEST_NUM_STATS;
	unsigned int sink_count = 0;
	unsigned int num_sources = OLD_MAX_SOURCES + 36; /* 100 sources, old limit was 64 */
	unsigned int i;
	int ret;

	sources = malloc(num_sources * sizeof(struct rte_sampler_source *));
	if (sources == NULL) {
		printf("Failed to allocate sources array\n");
		return TEST_FAILED;
	}

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		free(sources);
		return TEST_FAILED;
	}

	memset(&src_ops, 0, sizeof(src_ops));
	src_ops.xstats_names_get = test_xstats_names_get;
	src_ops.xstats_get = test_xstats_get;

	/* Register more sources than old static limit */
	for (i = 0; i < num_sources; i++) {
		char name[64];
		snprintf(name, sizeof(name), "test_source_%u", i);
		
		sources[i] = rte_sampler_source_register(session, name, i, &src_ops, &num_stats);
		if (sources[i] == NULL) {
			printf("Failed to register source %u (old limit was %u)\n", i, OLD_MAX_SOURCES);
			while (i > 0) {
				i--;
				rte_sampler_source_unregister(sources[i]);
			}
			rte_sampler_session_free(session);
			free(sources);
			return TEST_FAILED;
		}
	}

	memset(&sink_ops, 0, sizeof(sink_ops));
	sink_ops.output = test_sink_output;

	sink = rte_sampler_sink_register(session, "test_sink", &sink_ops, &sink_count);
	if (sink == NULL) {
		printf("Failed to register sink\n");
		for (i = 0; i < num_sources; i++)
			rte_sampler_source_unregister(sources[i]);
		rte_sampler_session_free(session);
		free(sources);
		return TEST_FAILED;
	}

	rte_sampler_session_start(session);
	ret = rte_sampler_sample(session);
	if (ret != 0) {
		printf("Sampling failed\n");
		rte_sampler_sink_unregister(sink);
		for (i = 0; i < num_sources; i++)
			rte_sampler_source_unregister(sources[i]);
		rte_sampler_session_free(session);
		free(sources);
		return TEST_FAILED;
	}

	if (sink_count != num_sources) {
		printf("Expected %u samples, got %u\n", num_sources, sink_count);
		rte_sampler_sink_unregister(sink);
		for (i = 0; i < num_sources; i++)
			rte_sampler_source_unregister(sources[i]);
		rte_sampler_session_free(session);
		free(sources);
		return TEST_FAILED;
	}

	rte_sampler_session_stop(session);
	rte_sampler_sink_unregister(sink);
	for (i = 0; i < num_sources; i++)
		rte_sampler_source_unregister(sources[i]);
	rte_sampler_session_free(session);
	free(sources);
	
	return TEST_SUCCESS;
}

/* Test multiple sessions beyond old static limit */
static int
test_sampler_dynamic_sessions(void)
{
	struct rte_sampler_session **sessions;
	struct rte_sampler_session_conf conf;
	unsigned int num_sessions = OLD_MAX_SESSIONS + 8; /* 40 sessions, old limit was 32 */
	unsigned int i;

	sessions = malloc(num_sessions * sizeof(struct rte_sampler_session *));
	if (sessions == NULL) {
		printf("Failed to allocate sessions array\n");
		return TEST_FAILED;
	}

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;

	/* Create more sessions than old static limit */
	for (i = 0; i < num_sessions; i++) {
		char name[64];
		snprintf(name, sizeof(name), "test_session_%u", i);
		conf.name = name;
		
		sessions[i] = rte_sampler_session_create(&conf);
		if (sessions[i] == NULL) {
			printf("Failed to create session %u (old limit was %u)\n", i, OLD_MAX_SESSIONS);
			while (i > 0) {
				i--;
				rte_sampler_session_free(sessions[i]);
			}
			free(sessions);
			return TEST_FAILED;
		}
	}

	/* Free all sessions */
	for (i = 0; i < num_sessions; i++)
		rte_sampler_session_free(sessions[i]);
	
	free(sessions);
	return TEST_SUCCESS;
}

/* Test filter functionality */
static int
test_sampler_filter(void)
{
	struct rte_sampler_session *session;
	struct rte_sampler_session_conf conf;
	struct rte_sampler_source *source;
	struct rte_sampler_source_ops ops;
	int num_stats = TEST_NUM_STATS;
	const char *patterns[] = {"test_stat_1*", "test_stat_2*"};
	int ret;

	memset(&conf, 0, sizeof(conf));
	conf.sample_interval_ms = 0;
	conf.duration_ms = 0;
	conf.name = "test_session";

	session = rte_sampler_session_create(&conf);
	if (session == NULL) {
		printf("Failed to create session\n");
		return TEST_FAILED;
	}

	memset(&ops, 0, sizeof(ops));
	ops.xstats_names_get = test_xstats_names_get;
	ops.xstats_get = test_xstats_get;

	source = rte_sampler_source_register(session, "test_source", 0, &ops, &num_stats);
	if (source == NULL) {
		printf("Failed to register source\n");
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	ret = rte_sampler_source_set_filter(source, patterns, 2);
	if (ret != 0) {
		printf("Failed to set filter\n");
		rte_sampler_source_unregister(source);
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	ret = rte_sampler_source_clear_filter(source);
	if (ret != 0) {
		printf("Failed to clear filter\n");
		rte_sampler_source_unregister(source);
		rte_sampler_session_free(session);
		return TEST_FAILED;
	}

	rte_sampler_source_unregister(source);
	rte_sampler_session_free(session);
	return TEST_SUCCESS;
}

static struct unit_test_suite sampler_tests = {
	.suite_name = "sampler autotest",
	.setup = NULL,
	.teardown = NULL,
	.unit_test_cases = {
		TEST_CASE(test_sampler_session_create_free),
		TEST_CASE(test_sampler_session_start_stop),
		TEST_CASE(test_sampler_source_register),
		TEST_CASE(test_sampler_sink_register),
		TEST_CASE(test_sampler_sample_basic),
		TEST_CASE(test_sampler_dynamic_sources),
		TEST_CASE(test_sampler_dynamic_sessions),
		TEST_CASE(test_sampler_filter),
		TEST_CASES_END()
	}
};

static int
test_sampler(void)
{
	return unit_test_suite_runner(&sampler_tests);
}

REGISTER_FAST_TEST(sampler_autotest, true, true, test_sampler);
