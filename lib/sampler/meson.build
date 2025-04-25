# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2025 Ericsson AB

if is_windows
    build = false
    reason = 'not supported on Windows'
    subdir_done()
endif

cflags += no_wvla_cflag

sources = files(
    'rte_sampler.c'
    'rte_event_dev_sampler.c',
    'rte_sampler_file_sink.c'
)
headers = files('rte_sampler.h')

deps += ['eventdev']
