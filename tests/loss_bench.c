/*
 * Deterministic selective-repeat benchmark with 10% loss in both
 * directions.  It exercises the same framing and ACK bitmap code used by
 * the DNS tunnel, but without requiring tun devices or root privileges.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/reliable.h"

#define PAYLOAD_SIZE 4096
#define FRAGMENT_SIZE 256
#define LOSS_PERCENT 10

static uint32_t rng_state = 0x28756u;

static uint32_t
rng_next(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

static int
lost(void)
{
	return (rng_next() % 100) < LOSS_PERCENT;
}

static int
test_ack_bitmap(void)
{
	char encoded[RELIABLE_CTRL_LEN];
	uint16_t bitmap;
	unsigned int i;

	for (i = 0; i <= 0xffff; i++) {
		uint16_t value = (uint16_t) i;

		if (reliable_ctl_encode(encoded, value) != RELIABLE_CTRL_LEN)
			return 0;
		if (!reliable_ctl_decode(encoded, sizeof(encoded), &bitmap) ||
		    bitmap != value)
			return 0;
	}
	memcpy(encoded, "E-APab", RELIABLE_CTRL_LEN);
	if (!reliable_ctl_decode(encoded, sizeof(encoded), &bitmap) ||
	    bitmap != 0x10f0)
		return 0;
	return !reliable_ctl_is("x0000", 5);
}

static int
test_selective_repeat(void)
{
	char source[PAYLOAD_SIZE];
	char slots[PAYLOAD_SIZE];
	struct reliable_tx tx;
	struct reliable_rx rx;
	unsigned int rounds = 0;
	unsigned int sent = 0;
	unsigned int i;
	size_t length;
	double efficiency;

	for (i = 0; i < sizeof(source); i++)
		source[i] = (char) (i * 37 + 11);
	memset(slots, 0, sizeof(slots));

	reliable_tx_reset(&tx, 3, sizeof(source), FRAGMENT_SIZE);
	reliable_rx_reset(&rx, 3);

	while (!reliable_tx_complete(&tx) && rounds < 100) {
		uint16_t ack;
		int fragment;

		/* A small window avoids an unrealistic go-back-N burst while
		 * still allowing independent fragments to fill the pipe. */
		for (fragment = 0; fragment < tx.count; fragment++) {
			if (tx.acked & (1U << fragment))
				continue;
			if ((unsigned int) fragment >= rounds * 4 + 4)
				break;

			sent++;
			if (lost())
				continue;

			if (!reliable_rx_put(&rx, tx.seq, fragment,
					     fragment == tx.count - 1,
					     tx.length[fragment],
					     FRAGMENT_SIZE,
					     source + tx.offset[fragment],
					     slots)) {
				fprintf(stderr, "receiver rejected fragment %d\n",
					fragment);
				return 0;
			}
		}

		/* The receiver's ACK bitmap can itself be lost. */
		if (!lost()) {
			ack = reliable_rx_bitmap(&rx);
			reliable_tx_ack(&tx, ack);
		}
		rounds++;
	}

	if (!reliable_tx_complete(&tx) || !reliable_rx_complete(&rx)) {
		fprintf(stderr, "transfer did not complete after %u rounds\n",
			rounds);
		return 0;
	}

	length = reliable_rx_assemble(&rx, slots, FRAGMENT_SIZE,
				      sizeof(slots));
	if (length != sizeof(source) ||
	    memcmp(source, slots, sizeof(source)) != 0) {
		fprintf(stderr, "reassembled payload mismatch\n");
		return 0;
	}

	efficiency = (double) PAYLOAD_SIZE / ((double) sent * FRAGMENT_SIZE);
	printf("loss=%d%% fragments=%u rounds=%u sent=%u "
	       "throughput=%.2f bytes/fragment (%.1f%% of wire)\n",
	       LOSS_PERCENT, tx.count, rounds, sent, efficiency * FRAGMENT_SIZE,
	       efficiency * 100.0);

	if (efficiency < 0.70) {
		fprintf(stderr, "throughput too low under %d%% loss\n",
			LOSS_PERCENT);
		return 0;
	}

	return 1;
}

static int
test_mtu_adaptation(void)
{
	struct reliable_mtu mtu;
	unsigned int i;
	unsigned int before;

	reliable_mtu_init(&mtu, 1200, 128, 1400);
	for (i = 0; i < 16; i++)
		reliable_mtu_success(&mtu, 1200);
	if (reliable_mtu_value(&mtu) <= 1200) {
		fprintf(stderr, "MTU did not grow after stable transfers\n");
		return 0;
	}

	before = reliable_mtu_value(&mtu);
	reliable_mtu_loss(&mtu);
	if (reliable_mtu_value(&mtu) >= before) {
		fprintf(stderr, "MTU did not shrink after loss\n");
		return 0;
	}

	for (i = 0; i < 64; i++)
		reliable_mtu_loss(&mtu);
	if (reliable_mtu_value(&mtu) != 128) {
		fprintf(stderr, "MTU floor not enforced\n");
		return 0;
	}

	return 1;
}

int
main(void)
{
	if (!test_ack_bitmap()) {
		fprintf(stderr, "ACK bitmap round-trip failed\n");
		return EXIT_FAILURE;
	}
	if (!test_selective_repeat())
		return EXIT_FAILURE;
	if (!test_mtu_adaptation())
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
