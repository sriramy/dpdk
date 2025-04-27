/* SPDX-License-Identifier: BSD-3-Clause
* Copyright(c) 2025 Ericsson AB
*/

#ifndef _RTE_SAMPLER_XSTATS_ID_H_
#define _RTE_SAMPLER_XSTATS_ID_H_

#include <stdbool.h>
#include <stdint.h>

void rte_sampler_xstats_set_next_free_id(uint64_t id);
void rte_sampler_xstats_set_id_bit(uint64_t id);
void rte_sampler_xstats_clear_id_bit(uint64_t id);
bool rte_sampler_xstats_is_id_used(uint64_t id);

#endif /* _RTE_SAMPLER_XSTATS_ID_H_ */
 