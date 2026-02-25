/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024 Intel Corporation
 *
 * Simple test program for memif multi-segment mbuf handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_bus_vdev.h>

#define MEMPOOL_CACHE_SIZE 256
#define NB_MBUF 8192
#define SEGMENT_SIZE 512
#define NUM_SEGMENTS 3
#define RX_RING_SIZE 256
#define TX_RING_SIZE 256

static struct rte_mempool *mbuf_pool = NULL;
static volatile int force_quit = 0;

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n", signum);
		force_quit = 1;
	}
}

static int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf = {0};
	struct rte_eth_dev_info dev_info;
	int retval;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error getting device info for port %u\n", port);
		return retval;
	}

	/* Configure device */
	retval = rte_eth_dev_configure(port, 1, 1, &port_conf);
	if (retval != 0)
		return retval;

	/* Setup RX queue */
	retval = rte_eth_rx_queue_setup(port, 0, nb_rxd,
			rte_eth_dev_socket_id(port), NULL, mbuf_pool);
	if (retval < 0)
		return retval;

	/* Setup TX queue */
	retval = rte_eth_tx_queue_setup(port, 0, nb_txd,
			rte_eth_dev_socket_id(port), NULL);
	if (retval < 0)
		return retval;

	/* Start device */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	printf("Port %u initialized successfully\n", port);
	return 0;
}

static struct rte_mbuf *
create_multi_segment_packet(struct rte_mempool *pool, uint32_t total_len)
{
	struct rte_mbuf *head, *seg, *prev;
	uint32_t remaining = total_len;
	uint32_t offset = 0;
	int seg_count = 0;

	/* Allocate head segment */
	head = rte_pktmbuf_alloc(pool);
	if (head == NULL)
		return NULL;

	prev = head;

	while (remaining > 0 && seg_count < NUM_SEGMENTS) {
		uint16_t seg_len = (remaining > SEGMENT_SIZE) ? SEGMENT_SIZE : remaining;
		uint8_t *data;

		if (seg_count > 0) {
			seg = rte_pktmbuf_alloc(pool);
			if (seg == NULL) {
				rte_pktmbuf_free(head);
				return NULL;
			}

			/* Chain segment */
			if (rte_pktmbuf_chain(head, seg) < 0) {
				rte_pktmbuf_free(seg);
				rte_pktmbuf_free(head);
				return NULL;
			}
			prev = seg;
		} else {
			seg = head;
		}

		/* Fill segment with test pattern */
		data = rte_pktmbuf_mtod(seg, uint8_t *);
		for (uint16_t i = 0; i < seg_len; i++) {
			data[i] = (uint8_t)((offset + i) & 0xFF);
		}
		rte_pktmbuf_data_len(seg) = seg_len;

		if (seg_count == 0)
			rte_pktmbuf_pkt_len(head) = seg_len;
		else
			rte_pktmbuf_pkt_len(head) += seg_len;

		remaining -= seg_len;
		offset += seg_len;
		seg_count++;
	}

	printf("Created multi-segment packet: nb_segs=%u, pkt_len=%u\n",
		   head->nb_segs, rte_pktmbuf_pkt_len(head));

	return head;
}

static int
verify_packet_data(struct rte_mbuf *m)
{
	struct rte_mbuf *seg = m;
	uint32_t offset = 0;
	int errors = 0;

	while (seg != NULL) {
		uint8_t *data = rte_pktmbuf_mtod(seg, uint8_t *);
		uint16_t len = rte_pktmbuf_data_len(seg);

		for (uint16_t i = 0; i < len; i++) {
			uint8_t expected = (uint8_t)((offset + i) & 0xFF);
			if (data[i] != expected) {
				printf("Data mismatch at offset %u: got 0x%02x, expected 0x%02x\n",
					   offset + i, data[i], expected);
				errors++;
				if (errors >= 10) /* Limit error output */
					return errors;
			}
		}
		offset += len;
		seg = seg->next;
	}

	return errors;
}

int
main(int argc, char *argv[])
{
	uint16_t server_port = 0, client_port = 1;
	int ret;
	char vdev_args[256];
	uint16_t nb_ports;
	struct rte_mbuf *tx_bufs[1];
	struct rte_mbuf *rx_bufs[1];
	uint16_t nb_tx, nb_rx;

	/* Initialize EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	force_quit = 0;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	/* Create mempool */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NB_MBUF,
			MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
			rte_socket_id());
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Create memif server */
	snprintf(vdev_args, sizeof(vdev_args),
			 "net_memif0,role=server,id=0,socket=/tmp/memif_test.sock");
	printf("Creating memif server: %s\n", vdev_args);
	ret = rte_vdev_init(vdev_args, NULL);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot create memif server\n");

	/* Create memif client */
	snprintf(vdev_args, sizeof(vdev_args),
			 "net_memif1,role=client,id=0,socket=/tmp/memif_test.sock");
	printf("Creating memif client: %s\n", vdev_args);
	ret = rte_vdev_init(vdev_args, NULL);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot create memif client\n");

	rte_delay_ms(100);

	nb_ports = rte_eth_dev_count_avail();
	printf("Number of available ports: %u\n", nb_ports);

	if (nb_ports < 2)
		rte_exit(EXIT_FAILURE, "Not enough ports available (need 2)\n");

	/* Initialize both ports */
	if (port_init(server_port, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init server port %u\n", server_port);

	if (port_init(client_port, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init client port %u\n", client_port);

	rte_delay_ms(100);

	printf("\n=== Starting multi-segment packet test ===\n");

	/* Create multi-segment packet */
	tx_bufs[0] = create_multi_segment_packet(mbuf_pool, SEGMENT_SIZE * NUM_SEGMENTS);
	if (tx_bufs[0] == NULL)
		rte_exit(EXIT_FAILURE, "Failed to create multi-segment packet\n");

	/* Transmit packet from server to client */
	printf("Transmitting multi-segment packet from port %u to port %u\n",
		   server_port, client_port);
	nb_tx = rte_eth_tx_burst(server_port, 0, tx_bufs, 1);
	printf("Transmitted %u packets\n", nb_tx);

	if (nb_tx != 1) {
		printf("ERROR: Failed to transmit packet\n");
		rte_pktmbuf_free(tx_bufs[0]);
		goto cleanup;
	}

	/* Wait for packet */
	rte_delay_ms(100);

	/* Receive packet */
	nb_rx = rte_eth_rx_burst(client_port, 0, rx_bufs, 1);
	printf("Received %u packets\n", nb_rx);

	if (nb_rx == 1) {
		printf("Received packet: nb_segs=%u, pkt_len=%u\n",
			   rx_bufs[0]->nb_segs, rte_pktmbuf_pkt_len(rx_bufs[0]));

		/* Verify data */
		int errors = verify_packet_data(rx_bufs[0]);
		if (errors == 0) {
			printf("\n=== TEST PASSED: Multi-segment packet transmitted and received successfully ===\n");
		} else {
			printf("\n=== TEST FAILED: Data verification errors: %d ===\n", errors);
		}

		rte_pktmbuf_free(rx_bufs[0]);
	} else {
		printf("\n=== TEST FAILED: No packet received ===\n");
	}

	/* Test completed */
	printf("\nTest completed. Cleaning up...\n");

cleanup:
	/* Stop ports */
	if (rte_eth_dev_is_valid_port(server_port))
		rte_eth_dev_stop(server_port);
	if (rte_eth_dev_is_valid_port(client_port))
		rte_eth_dev_stop(client_port);

	/* Cleanup */
	unlink("/tmp/memif_test.sock");
	rte_eal_cleanup();

	return 0;
}
