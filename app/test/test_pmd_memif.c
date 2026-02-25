/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 */
#include "test.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <rte_ethdev.h>
#include <rte_bus_vdev.h>
#include <rte_mbuf.h>

#define SOCKET0 0
#define RING_SIZE 256
#define NB_MBUF 512
#define MEMIF_SOCKET_PATH "/tmp/memif_test.sock"
#define SEGMENT_SIZE 256
#define NUM_SEGMENTS 3

static struct rte_mempool *mp;
static uint16_t server_port = RTE_MAX_ETHPORTS;
static uint16_t client_port = RTE_MAX_ETHPORTS;

static int
memif_setup(void)
{
	char vdev_args[256];
	int ret;
	struct rte_eth_conf null_conf;

	memset(&null_conf, 0, sizeof(struct rte_eth_conf));

	/* Create mempool */
	mp = rte_pktmbuf_pool_create("memif_test_pool", NB_MBUF, 32,
				     0, RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET0);
	if (mp == NULL) {
		printf("Failed to create mempool\n");
		return -1;
	}

	/* Create server memif interface */
	snprintf(vdev_args, sizeof(vdev_args),
		 "net_memif0,role=server,id=0,socket=%s,bsize=2048",
		 MEMIF_SOCKET_PATH);
	ret = rte_vdev_init(vdev_args, NULL);
	if (ret < 0) {
		printf("Failed to create server memif: %s\n", vdev_args);
		goto cleanup_mp;
	}

	/* Find server port */
	uint16_t port_id;
	RTE_ETH_FOREACH_DEV(port_id) {
		char name[RTE_ETH_NAME_MAX_LEN];
		rte_eth_dev_get_name_by_port(port_id, name);
		if (strstr(name, "net_memif0")) {
			server_port = port_id;
			break;
		}
	}
	if (server_port == RTE_MAX_ETHPORTS) {
		printf("Failed to find server port\n");
		goto cleanup_vdev;
	}

	/* Configure server port */
	if (rte_eth_dev_configure(server_port, 1, 1, &null_conf) < 0) {
		printf("Configure failed for server port\n");
		goto cleanup_vdev;
	}

	if (rte_eth_tx_queue_setup(server_port, 0, RING_SIZE, SOCKET0, NULL) < 0) {
		printf("TX queue setup failed for server port\n");
		goto cleanup_vdev;
	}

	if (rte_eth_rx_queue_setup(server_port, 0, RING_SIZE, SOCKET0, NULL, mp) < 0) {
		printf("RX queue setup failed for server port\n");
		goto cleanup_vdev;
	}

	if (rte_eth_dev_start(server_port) < 0) {
		printf("Error starting server port\n");
		goto cleanup_vdev;
	}

	/* Create client memif interface */
	snprintf(vdev_args, sizeof(vdev_args),
		 "net_memif1,role=client,id=0,socket=%s,bsize=2048",
		 MEMIF_SOCKET_PATH);
	ret = rte_vdev_init(vdev_args, NULL);
	if (ret < 0) {
		printf("Failed to create client memif: %s\n", vdev_args);
		goto cleanup_server;
	}

	/* Find client port */
	RTE_ETH_FOREACH_DEV(port_id) {
		char name[RTE_ETH_NAME_MAX_LEN];
		rte_eth_dev_get_name_by_port(port_id, name);
		if (strstr(name, "net_memif1")) {
			client_port = port_id;
			break;
		}
	}
	if (client_port == RTE_MAX_ETHPORTS) {
		printf("Failed to find client port\n");
		goto cleanup_client_vdev;
	}

	/* Configure client port */
	if (rte_eth_dev_configure(client_port, 1, 1, &null_conf) < 0) {
		printf("Configure failed for client port\n");
		goto cleanup_client_vdev;
	}

	if (rte_eth_tx_queue_setup(client_port, 0, RING_SIZE, SOCKET0, NULL) < 0) {
		printf("TX queue setup failed for client port\n");
		goto cleanup_client_vdev;
	}

	if (rte_eth_rx_queue_setup(client_port, 0, RING_SIZE, SOCKET0, NULL, mp) < 0) {
		printf("RX queue setup failed for client port\n");
		goto cleanup_client_vdev;
	}

	if (rte_eth_dev_start(client_port) < 0) {
		printf("Error starting client port\n");
		goto cleanup_client_vdev;
	}

	/* Give some time for connection establishment */
	rte_delay_ms(100);

	printf("Memif setup complete: server_port=%u, client_port=%u\n",
	       server_port, client_port);
	return 0;

cleanup_client_vdev:
	if (client_port != RTE_MAX_ETHPORTS) {
		rte_eth_dev_stop(client_port);
		rte_vdev_uninit("net_memif1");
		client_port = RTE_MAX_ETHPORTS;
	}
cleanup_server:
	if (server_port != RTE_MAX_ETHPORTS) {
		rte_eth_dev_stop(server_port);
	}
cleanup_vdev:
	rte_vdev_uninit("net_memif0");
	server_port = RTE_MAX_ETHPORTS;
cleanup_mp:
	rte_mempool_free(mp);
	mp = NULL;
	return -1;
}

static void
memif_teardown(void)
{
	if (client_port != RTE_MAX_ETHPORTS) {
		rte_eth_dev_stop(client_port);
		rte_vdev_uninit("net_memif1");
		client_port = RTE_MAX_ETHPORTS;
	}

	if (server_port != RTE_MAX_ETHPORTS) {
		rte_eth_dev_stop(server_port);
		rte_vdev_uninit("net_memif0");
		server_port = RTE_MAX_ETHPORTS;
	}

	if (mp != NULL) {
		rte_mempool_free(mp);
		mp = NULL;
	}

	/* Clean up socket file */
	unlink(MEMIF_SOCKET_PATH);
}

static int
test_memif_multi_segment_tx_rx(void)
{
	struct rte_mbuf *tx_bufs[1];
	struct rte_mbuf *rx_bufs[1];
	struct rte_mbuf *m, *seg;
	uint16_t nb_tx, nb_rx;
	uint32_t total_data_len = 0;
	uint32_t rx_data_len = 0;
	uint8_t *data_ptr;
	int i;

	printf("Testing multi-segment mbuf transmission and reception\n");

	/* Allocate head mbuf */
	m = rte_pktmbuf_alloc(mp);
	if (m == NULL) {
		printf("Failed to allocate head mbuf\n");
		return -1;
	}

	tx_bufs[0] = m;

	/* Add data to head segment */
	data_ptr = rte_pktmbuf_mtod(m, uint8_t *);
	for (i = 0; i < SEGMENT_SIZE && i < rte_pktmbuf_tailroom(m); i++) {
		data_ptr[i] = (uint8_t)(i & 0xFF);
	}
	rte_pktmbuf_data_len(m) = i;
	rte_pktmbuf_pkt_len(m) = i;
	total_data_len = i;

	/* Chain additional segments */
	for (int seg_idx = 1; seg_idx < NUM_SEGMENTS; seg_idx++) {
		seg = rte_pktmbuf_alloc(mp);
		if (seg == NULL) {
			printf("Failed to allocate segment %d\n", seg_idx);
			rte_pktmbuf_free(m);
			return -1;
		}

		/* Add data to segment */
		data_ptr = rte_pktmbuf_mtod(seg, uint8_t *);
		for (i = 0; i < SEGMENT_SIZE && i < rte_pktmbuf_tailroom(seg); i++) {
			data_ptr[i] = (uint8_t)((seg_idx * SEGMENT_SIZE + i) & 0xFF);
		}
		rte_pktmbuf_data_len(seg) = i;
		total_data_len += i;

		/* Chain segment */
		if (rte_pktmbuf_chain(m, seg) < 0) {
			printf("Failed to chain segment %d\n", seg_idx);
			rte_pktmbuf_free(seg);
			rte_pktmbuf_free(m);
			return -1;
		}
	}

	printf("Created multi-segment mbuf: nb_segs=%u, pkt_len=%u, total_data=%u\n",
	       m->nb_segs, rte_pktmbuf_pkt_len(m), total_data_len);

	/* Transmit from server to client */
	nb_tx = rte_eth_tx_burst(server_port, 0, tx_bufs, 1);
	if (nb_tx != 1) {
		printf("Failed to transmit packet: nb_tx=%u\n", nb_tx);
		rte_pktmbuf_free(m);
		return -1;
	}

	printf("Transmitted %u multi-segment packet(s)\n", nb_tx);

	/* Give time for packet to be received */
	rte_delay_ms(10);

	/* Receive on client */
	nb_rx = rte_eth_rx_burst(client_port, 0, rx_bufs, 1);
	if (nb_rx != 1) {
		printf("Failed to receive packet: nb_rx=%u (expected 1)\n", nb_rx);
		return -1;
	}

	printf("Received %u packet(s)\n", nb_rx);

	/* Verify received packet */
	m = rx_bufs[0];
	printf("Received mbuf: nb_segs=%u, pkt_len=%u\n",
	       m->nb_segs, rte_pktmbuf_pkt_len(m));

	/* Verify data integrity */
	seg = m;
	uint32_t offset = 0;
	while (seg != NULL) {
		data_ptr = rte_pktmbuf_mtod(seg, uint8_t *);
		uint16_t seg_len = rte_pktmbuf_data_len(seg);
		rx_data_len += seg_len;

		for (i = 0; i < seg_len; i++) {
			uint8_t expected = (uint8_t)((offset + i) & 0xFF);
			if (data_ptr[i] != expected) {
				printf("Data mismatch at offset %u: got 0x%02x, expected 0x%02x\n",
				       offset + i, data_ptr[i], expected);
				rte_pktmbuf_free(m);
				return -1;
			}
		}
		offset += seg_len;
		seg = seg->next;
	}

	printf("Data verification passed: verified %u bytes\n", rx_data_len);

	if (rx_data_len != total_data_len) {
		printf("Data length mismatch: rx=%u, tx=%u\n",
		       rx_data_len, total_data_len);
		rte_pktmbuf_free(m);
		return -1;
	}

	rte_pktmbuf_free(m);

	printf("Multi-segment test PASSED\n");
	return 0;
}

static int
test_memif_incomplete_segment_handling(void)
{
	struct rte_eth_stats stats_before, stats_after;
	struct rte_mbuf *tx_bufs[1];
	struct rte_mbuf *m;
	int ret;

	printf("Testing incomplete multi-segment packet handling\n");

	/* Get initial stats */
	ret = rte_eth_stats_get(server_port, &stats_before);
	if (ret < 0) {
		printf("Failed to get initial stats\n");
		return -1;
	}

	/* Allocate a simple single-segment packet for baseline */
	m = rte_pktmbuf_alloc(mp);
	if (m == NULL) {
		printf("Failed to allocate mbuf\n");
		return -1;
	}

	uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
	memset(data, 0xAA, 64);
	rte_pktmbuf_data_len(m) = 64;
	rte_pktmbuf_pkt_len(m) = 64;

	tx_bufs[0] = m;

	/* Transmit */
	uint16_t nb_tx = rte_eth_tx_burst(server_port, 0, tx_bufs, 1);
	if (nb_tx != 1) {
		printf("Failed to transmit test packet\n");
		rte_pktmbuf_free(m);
		return -1;
	}

	/* Get stats after */
	rte_delay_ms(10);
	ret = rte_eth_stats_get(server_port, &stats_after);
	if (ret < 0) {
		printf("Failed to get stats after transmission\n");
		return -1;
	}

	printf("Stats before: opackets=%lu, oerrors=%lu\n",
	       stats_before.opackets, stats_before.oerrors);
	printf("Stats after:  opackets=%lu, oerrors=%lu\n",
	       stats_after.opackets, stats_after.oerrors);

	if (stats_after.opackets <= stats_before.opackets) {
		printf("No packets transmitted\n");
		return -1;
	}

	printf("Incomplete segment handling test PASSED\n");
	return 0;
}

static int
test_pmd_memif(void)
{
	if (memif_setup() < 0) {
		printf("Memif setup failed\n");
		memif_teardown();
		return -1;
	}

	int ret = 0;

	/* Test multi-segment TX/RX */
	if (test_memif_multi_segment_tx_rx() < 0) {
		printf("Multi-segment TX/RX test failed\n");
		ret = -1;
	}

	/* Test incomplete segment handling */
	if (test_memif_incomplete_segment_handling() < 0) {
		printf("Incomplete segment handling test failed\n");
		ret = -1;
	}

	memif_teardown();

	if (ret == 0) {
		printf("\n=== All memif multi-segment tests PASSED ===\n");
	} else {
		printf("\n=== Some memif multi-segment tests FAILED ===\n");
	}

	return ret;
}

REGISTER_FAST_TEST(memif_pmd_autotest, NOHUGE_SKIP, ASAN_OK, test_pmd_memif);
