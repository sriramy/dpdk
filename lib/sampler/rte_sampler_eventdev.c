/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */

#include <string.h>
#include <eal_export.h>
#include <rte_common.h>
#include <rte_eventdev.h>
#include <rte_malloc.h>
#include <rte_sampler.h>
#include <rte_sampler_eventdev.h>

/**
 * Eventdev source user data
 */
struct eventdev_source_data {
enum rte_sampler_eventdev_mode mode;
uint8_t queue_port_id;
};

/**
 * Eventdev xstats_names_get callback
 */
static int
eventdev_xstats_names_get(uint16_t source_id,
  struct rte_sampler_xstats_name *xstats_names,
  uint64_t *ids,
  unsigned int size,
  void *user_data)
{
struct eventdev_source_data *data = user_data;
struct rte_event_dev_xstats_name *eventdev_names = NULL;
enum rte_event_dev_xstats_mode mode;
int ret;
unsigned int i;

/* Map sampler mode to eventdev mode */
switch (data->mode) {
case RTE_SAMPLER_EVENTDEV_DEVICE:
mode = RTE_EVENT_DEV_XSTATS_DEVICE;
break;
case RTE_SAMPLER_EVENTDEV_PORT:
mode = RTE_EVENT_DEV_XSTATS_PORT;
break;
case RTE_SAMPLER_EVENTDEV_QUEUE:
mode = RTE_EVENT_DEV_XSTATS_QUEUE;
break;
default:
return -EINVAL;
}

/* Allocate temporary buffer for eventdev names */
if (xstats_names != NULL) {
eventdev_names = rte_malloc(NULL,
    sizeof(*eventdev_names) * size,
    0);
if (eventdev_names == NULL)
return -ENOMEM;
}

/* Get xstats names from eventdev */
ret = rte_event_dev_xstats_names_get((uint8_t)source_id,
     mode,
     data->queue_port_id,
     eventdev_names,
     ids,
     size);

/* Copy names to sampler format */
if (ret > 0 && xstats_names != NULL && eventdev_names != NULL) {
for (i = 0; i < (unsigned int)ret && i < size; i++) {
rte_strscpy(xstats_names[i].name,
   eventdev_names[i].name,
   RTE_SAMPLER_XSTATS_NAME_SIZE);
}
}

rte_free(eventdev_names);

return ret;
}

/**
 * Eventdev xstats_get callback
 */
static int
eventdev_xstats_get(uint16_t source_id,
    const uint64_t *ids,
    uint64_t *values,
    unsigned int n,
    void *user_data)
{
struct eventdev_source_data *data = user_data;
enum rte_event_dev_xstats_mode mode;

/* Map sampler mode to eventdev mode */
switch (data->mode) {
case RTE_SAMPLER_EVENTDEV_DEVICE:
mode = RTE_EVENT_DEV_XSTATS_DEVICE;
break;
case RTE_SAMPLER_EVENTDEV_PORT:
mode = RTE_EVENT_DEV_XSTATS_PORT;
break;
case RTE_SAMPLER_EVENTDEV_QUEUE:
mode = RTE_EVENT_DEV_XSTATS_QUEUE;
break;
default:
return -EINVAL;
}

return rte_event_dev_xstats_get((uint8_t)source_id,
mode,
data->queue_port_id,
ids,
values,
n);
}

/**
 * Eventdev xstats_reset callback
 */
static int
eventdev_xstats_reset(uint16_t source_id,
      const uint64_t *ids,
      unsigned int n,
      void *user_data)
{
struct eventdev_source_data *data = user_data;
enum rte_event_dev_xstats_mode mode;
int16_t queue_port_id;

/* Map sampler mode to eventdev mode */
switch (data->mode) {
case RTE_SAMPLER_EVENTDEV_DEVICE:
mode = RTE_EVENT_DEV_XSTATS_DEVICE;
queue_port_id = -1;
break;
case RTE_SAMPLER_EVENTDEV_PORT:
mode = RTE_EVENT_DEV_XSTATS_PORT;
queue_port_id = data->queue_port_id;
break;
case RTE_SAMPLER_EVENTDEV_QUEUE:
mode = RTE_EVENT_DEV_XSTATS_QUEUE;
queue_port_id = data->queue_port_id;
break;
default:
return -EINVAL;
}

return rte_event_dev_xstats_reset((uint8_t)source_id,
  mode,
  queue_port_id,
  ids,
  n);
}

RTE_EXPORT_SYMBOL(rte_sampler_eventdev_source_register)
int
rte_sampler_eventdev_source_register(struct rte_sampler *sampler,
      uint8_t dev_id,
      const struct rte_sampler_eventdev_conf *conf)
{
struct rte_sampler_source_ops ops;
struct eventdev_source_data *data;
char source_name[RTE_SAMPLER_XSTATS_NAME_SIZE];
int ret;

if (sampler == NULL || conf == NULL)
return -EINVAL;

/* Allocate user data */
data = rte_malloc(NULL, sizeof(*data), 0);
if (data == NULL)
return -ENOMEM;

data->mode = conf->mode;
data->queue_port_id = conf->queue_port_id;

/* Setup operations */
ops.xstats_names_get = eventdev_xstats_names_get;
ops.xstats_get = eventdev_xstats_get;
ops.xstats_reset = eventdev_xstats_reset;

/* Create source name */
snprintf(source_name, sizeof(source_name), "eventdev_%u", dev_id);

/* Register source */
ret = rte_sampler_source_register(sampler, source_name, dev_id,
   &ops, data);
if (ret < 0) {
rte_free(data);
return ret;
}

return ret;
}
