/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright 2018 Gaëtan Rivet
 */

#include <stdio.h>
#include <string.h>
#include <sys/queue.h>

#include <rte_class.h>
#include <rte_debug.h>

#include <eal_export.h>
#include "eal_private.h"

static struct rte_class_list rte_class_list =
	TAILQ_HEAD_INITIALIZER(rte_class_list);

RTE_EXPORT_SYMBOL(rte_class_register)
void
rte_class_register(struct rte_class *class)
{
	RTE_VERIFY(class);
	RTE_VERIFY(class->name && strlen(class->name));

	TAILQ_INSERT_TAIL(&rte_class_list, class, next);
	EAL_LOG(DEBUG, "Registered [%s] device class.", class->name);
}

RTE_EXPORT_SYMBOL(rte_class_unregister)
void
rte_class_unregister(struct rte_class *class)
{
	TAILQ_REMOVE(&rte_class_list, class, next);
	EAL_LOG(DEBUG, "Unregistered [%s] device class.", class->name);
}

RTE_EXPORT_SYMBOL(rte_class_find)
struct rte_class *
rte_class_find(const struct rte_class *start, rte_class_cmp_t cmp,
	       const void *data)
{
	struct rte_class *cls;

	if (start != NULL)
		cls = TAILQ_NEXT(start, next);
	else
		cls = TAILQ_FIRST(&rte_class_list);
	while (cls != NULL) {
		if (cmp(cls, data) == 0)
			break;
		cls = TAILQ_NEXT(cls, next);
	}
	return cls;
}

static int
cmp_class_name(const struct rte_class *class, const void *_name)
{
	const char *name = _name;

	return strcmp(class->name, name);
}

RTE_EXPORT_SYMBOL(rte_class_find_by_name)
struct rte_class *
rte_class_find_by_name(const char *name)
{
	return rte_class_find(NULL, cmp_class_name, (const void *)name);
}
