/*
 * Copyright (c) 2026
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __RELIABLE_H__
#define __RELIABLE_H__

#include <stddef.h>
#include <stdint.h>

/*
 * The wire data header has four fragment bits, so a packet can contain at
 * most sixteen fragments.  Both the receiver bitmap and the transmitter
 * bitmap use the same limit.
 */
#define RELIABLE_MAX_FRAGS 16

/* Prefix for the optional bitmap trailer used by reliable-mode peers.
 * The '-' keeps the control unambiguous: it cannot occur in an encoded
 * payload, while 'e' keeps the DNS label valid. */
#define RELIABLE_CTRL_MARKER "e-"
#define RELIABLE_CTRL_MARKER_LEN 2
#define RELIABLE_CTRL_LEN (RELIABLE_CTRL_MARKER_LEN + 4)

struct reliable_tx {
	uint16_t seq;
	uint16_t acked;
	uint8_t count;
	uint16_t offset[RELIABLE_MAX_FRAGS];
	uint16_t length[RELIABLE_MAX_FRAGS];
};

struct reliable_rx {
	uint16_t seq;
	uint16_t seen;
	uint8_t last_fragment;
	uint8_t have_last;
	uint16_t length[RELIABLE_MAX_FRAGS];
};

/*
 * Adaptive path MTU.  Successful transfers grow the size additively after a
 * stable period; loss cuts it multiplicatively.  This is intentionally
 * conservative: strict resolvers and middleboxes often discard oversized UDP
 * responses without returning ICMP.
 */
struct reliable_mtu {
	unsigned int current;
	unsigned int floor;
	unsigned int ceiling;
	unsigned int stable;
	unsigned int loss_streak;
};

int reliable_ctl_encode(char *dst, uint16_t bitmap);
int reliable_ctl_decode(const char *src, size_t srclen, uint16_t *bitmap);
int reliable_ctl_is(const char *src, size_t srclen);

void reliable_tx_reset(struct reliable_tx *tx, uint16_t seq,
		       size_t total, size_t fragment_size);
int reliable_tx_next(const struct reliable_tx *tx);
int reliable_tx_ack(struct reliable_tx *tx, uint16_t bitmap);
int reliable_tx_ack_fragment(struct reliable_tx *tx, unsigned int fragment);
int reliable_tx_complete(const struct reliable_tx *tx);

void reliable_rx_reset(struct reliable_rx *rx, uint16_t seq);
int reliable_rx_put(struct reliable_rx *rx, uint16_t seq,
		    unsigned int fragment, int is_last, size_t length,
		    size_t slot_size, const void *src, void *slot_data);
int reliable_rx_complete(const struct reliable_rx *rx);
uint16_t reliable_rx_bitmap(const struct reliable_rx *rx);
size_t reliable_rx_assemble(struct reliable_rx *rx, void *data,
			    size_t slot_size, size_t capacity);

void reliable_mtu_init(struct reliable_mtu *mtu, unsigned int current,
		       unsigned int floor, unsigned int ceiling);
void reliable_mtu_success(struct reliable_mtu *mtu, size_t used);
void reliable_mtu_loss(struct reliable_mtu *mtu);
unsigned int reliable_mtu_value(const struct reliable_mtu *mtu);

#endif /* __RELIABLE_H__ */
