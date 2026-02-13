/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#ifndef _RTE_SAMPLER_EVENTDEV_H_
#define _RTE_SAMPLER_EVENTDEV_H_

/**
 * @file
 * RTE Sampler Eventdev Source
 *
 * Eventdev source implementation for the sampler library.
 * Provides functions to register eventdev as a sampler source.
 */

#include <stdint.h>
#include <rte_sampler.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Eventdev sampler mode
 */
enum rte_sampler_eventdev_mode {
RTE_SAMPLER_EVENTDEV_DEVICE = 0,  /**< Sample device-level xstats */
RTE_SAMPLER_EVENTDEV_PORT,        /**< Sample port-level xstats */
RTE_SAMPLER_EVENTDEV_QUEUE,       /**< Sample queue-level xstats */
};

/**
 * Eventdev sampler configuration
 */
struct rte_sampler_eventdev_conf {
enum rte_sampler_eventdev_mode mode;  /**< Sampling mode */
uint8_t queue_port_id;                 /**< Queue or port ID (mode dependent) */
};

/**
 * Register an eventdev as a sampler source
 *
 * @param sampler
 *   Pointer to sampler structure
 * @param dev_id
 *   Eventdev device identifier
 * @param conf
 *   Pointer to eventdev sampler configuration
 * @return
 *   Source handle on success, negative on error
 */
int rte_sampler_eventdev_source_register(struct rte_sampler *sampler,
  uint8_t dev_id,
  const struct rte_sampler_eventdev_conf *conf);

#ifdef __cplusplus
}
#endif

#endif /* _RTE_SAMPLER_EVENTDEV_H_ */
