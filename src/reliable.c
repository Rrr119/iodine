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

#include <string.h>

#include "reliable.h"

static const char rel_b32[] = "abcdefghijklmnopqrstuvwxyz012345";

int
reliable_ctl_encode(char *dst, uint16_t bitmap)
{
	dst[0] = RELIABLE_CTRL_MARKER[0];
	dst[1] = RELIABLE_CTRL_MARKER[1];
	dst[2] = rel_b32[bitmap & 0x0f];
	dst[3] = rel_b32[(bitmap >> 4) & 0x0f];
	dst[4] = rel_b32[(bitmap >> 8) & 0x0f];
	dst[5] = rel_b32[(bitmap >> 12) & 0x0f];
	return RELIABLE_CTRL_LEN;
}

int
reliable_ctl_decode(const char *src, size_t srclen, uint16_t *bitmap)
{
	unsigned int i;
	unsigned int value = 0;
	char marker;

	if (!src || !bitmap || srclen < RELIABLE_CTRL_LEN)
		return 0;
	marker = src[0];
	if (marker >= 'A' && marker <= 'Z')
		marker = (char) (marker | 0x20);
	if (marker != RELIABLE_CTRL_MARKER[0] ||
	    src[1] != RELIABLE_CTRL_MARKER[1])
		return 0;

	for (i = RELIABLE_CTRL_MARKER_LEN; i < RELIABLE_CTRL_LEN; i++) {
		char c = src[i];
		const char *p;
		unsigned int nibble;

		if (!c)
			return 0;
		if (c >= 'A' && c <= 'Z')
			c = (char) (c | 0x20);
		p = strchr(rel_b32, c);
		if (!p)
			return 0;
		nibble = (unsigned int)(p - rel_b32);
		if (nibble > 0x0f)
			return 0;
		value |= nibble << ((i - RELIABLE_CTRL_MARKER_LEN) * 4);
	}

	*bitmap = (uint16_t) value;
	return 1;
}

int
reliable_ctl_is(const char *src, size_t srclen)
{
	uint16_t ignored;

	return reliable_ctl_decode(src, srclen, &ignored);
}

void
reliable_tx_reset(struct reliable_tx *tx, uint16_t seq,
		  size_t total, size_t fragment_size)
{
	unsigned int i;
	size_t offset = 0;

	memset(tx, 0, sizeof(*tx));
	tx->seq = seq;
	if (!fragment_size || !total)
		return;

	for (i = 0; i < RELIABLE_MAX_FRAGS && offset < total; i++) {
		size_t length = total - offset;

		if (length > fragment_size)
			length = fragment_size;
		tx->offset[i] = (uint16_t) offset;
		tx->length[i] = (uint16_t) length;
		tx->count++;
		offset += length;
	}
}

int
reliable_tx_next(const struct reliable_tx *tx)
{
	unsigned int i;

	for (i = 0; i < tx->count; i++) {
		if (!(tx->acked & (1U << i)))
			return (int) i;
	}
	return -1;
}

int
reliable_tx_ack(struct reliable_tx *tx, uint16_t bitmap)
{
	uint16_t before = tx->acked;

	tx->acked |= bitmap;
	if (tx->count < RELIABLE_MAX_FRAGS) {
		uint16_t valid = (uint16_t)((1U << tx->count) - 1);

		tx->acked &= valid;
	}
	return tx->acked != before;
}

int
reliable_tx_ack_fragment(struct reliable_tx *tx, unsigned int fragment)
{
	if (fragment >= tx->count)
		return 0;
	return reliable_tx_ack(tx, (uint16_t)(1U << fragment));
}

int
reliable_tx_complete(const struct reliable_tx *tx)
{
	uint16_t valid;

	if (!tx->count)
		return 1;
	valid = tx->count == RELIABLE_MAX_FRAGS
		? 0xffff
		: (uint16_t)((1U << tx->count) - 1);
	return (tx->acked & valid) == valid;
}

void
reliable_rx_reset(struct reliable_rx *rx, uint16_t seq)
{
	memset(rx, 0, sizeof(*rx));
	rx->seq = seq;
}

int
reliable_rx_put(struct reliable_rx *rx, uint16_t seq,
		unsigned int fragment, int is_last, size_t length,
		size_t slot_size, const void *src, void *slot_data)
{
	if (fragment >= RELIABLE_MAX_FRAGS || length > slot_size)
		return 0;

	if (seq != rx->seq) {
		reliable_rx_reset(rx, seq);
	} else if (rx->seen & (1U << fragment)) {
		return 0;
	}
	if ((rx->have_last && fragment > rx->last_fragment) ||
	    reliable_rx_complete(rx))
		return 0;

	if (length && src && slot_data)
		memcpy((char *) slot_data + fragment * slot_size, src, length);
	rx->length[fragment] = (uint16_t) length;
	rx->seen |= (uint16_t)(1U << fragment);
	if (is_last) {
		rx->last_fragment = (uint8_t) fragment;
		rx->have_last = 1;
	}
	return 1;
}

int
reliable_rx_complete(const struct reliable_rx *rx)
{
	uint16_t needed;

	if (!rx->have_last)
		return 0;
	needed = (uint16_t)((1U << (rx->last_fragment + 1)) - 1);
	return (rx->seen & needed) == needed;
}

uint16_t
reliable_rx_bitmap(const struct reliable_rx *rx)
{
	return rx->seen;
}

size_t
reliable_rx_assemble(struct reliable_rx *rx, void *data,
		     size_t slot_size, size_t capacity)
{
	size_t out = 0;
	unsigned int i;

	if (!reliable_rx_complete(rx))
		return 0;

	for (i = 0; i <= rx->last_fragment; i++) {
		if (rx->length[i] > capacity - out)
			return 0;
		memmove((char *) data + out,
			(char *) data + i * slot_size, rx->length[i]);
		out += rx->length[i];
	}
	return out;
}

void
reliable_mtu_init(struct reliable_mtu *mtu, unsigned int current,
		  unsigned int floor, unsigned int ceiling)
{
	if (!floor)
		floor = 1;
	if (ceiling < floor)
		ceiling = floor;
	if (current < floor)
		current = floor;
	if (current > ceiling)
		current = ceiling;

	mtu->current = current;
	mtu->floor = floor;
	mtu->ceiling = ceiling;
	mtu->stable = 0;
	mtu->loss_streak = 0;
}

void
reliable_mtu_success(struct reliable_mtu *mtu, size_t used)
{
	unsigned int step;

	(void) used;
	if (mtu->loss_streak)
		mtu->loss_streak--;
	if (mtu->current >= mtu->ceiling)
		return;

	mtu->stable++;
	if (mtu->stable < 8)
		return;

	step = mtu->current / 8;
	if (!step)
		step = 1;
	mtu->current += step;
	if (mtu->current > mtu->ceiling)
		mtu->current = mtu->ceiling;
	mtu->stable = 0;
}

void
reliable_mtu_loss(struct reliable_mtu *mtu)
{
	unsigned int reduced;

	if (mtu->loss_streak < 32)
		mtu->loss_streak++;
	reduced = mtu->current - (mtu->current / 8);
	if (reduced >= mtu->current)
		reduced = mtu->current - 1;
	if (reduced < mtu->floor)
		reduced = mtu->floor;
	mtu->current = reduced;
	mtu->stable = 0;
}

unsigned int
reliable_mtu_value(const struct reliable_mtu *mtu)
{
	return mtu->current;
}
