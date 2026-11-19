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

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "src/reliable.h"
#include "test_main.h"

START_TEST(test_control_bitmap)
{
	char encoded[RELIABLE_CTRL_LEN];
	uint16_t bitmap;
	unsigned int i;

	for (i = 0; i <= 0xffff; i++) {
		uint16_t value = (uint16_t) i;

		ck_assert_int_eq(reliable_ctl_encode(encoded, value),
				 RELIABLE_CTRL_LEN);
		ck_assert_int_eq(reliable_ctl_decode(encoded,
						     sizeof(encoded),
						     &bitmap), 1);
		ck_assert_uint_eq(bitmap, value);
	}
	memcpy(encoded, "E-APab", RELIABLE_CTRL_LEN);
	ck_assert_int_eq(reliable_ctl_decode(encoded, sizeof(encoded),
					     &bitmap), 1);
	ck_assert_uint_eq(bitmap, 0x10f0);
	ck_assert_int_eq(reliable_ctl_is("x0000", 5), 0);
}
END_TEST

START_TEST(test_selective_repeat)
{
	char payload[1024];
	char slots[1024];
	struct reliable_tx tx;
	struct reliable_rx rx;
	size_t assembled;
	unsigned int i;

	for (i = 0; i < sizeof(payload); i++)
		payload[i] = (char) (i + 17);

	reliable_tx_reset(&tx, 5, sizeof(payload), 128);
	reliable_rx_reset(&rx, 5);
	ck_assert_int_eq(tx.count, 8);

	/* Deliver in an intentionally surprising order. */
	for (i = 0; i < tx.count; i++) {
		unsigned int fragment = (i * 5) % tx.count;

		ck_assert_int_eq(reliable_rx_put(
			&rx, tx.seq, fragment,
			fragment == (unsigned int) tx.count - 1,
			tx.length[fragment], 128,
			payload + tx.offset[fragment], slots), 1);
	}

	ck_assert_int_eq(reliable_rx_complete(&rx), 1);
	ck_assert_uint_eq(reliable_rx_bitmap(&rx), (1U << tx.count) - 1);
	assembled = reliable_rx_assemble(&rx, slots, 128, sizeof(slots));
	ck_assert_uint_eq(assembled, sizeof(payload));
	ck_assert_mem_eq(slots, payload, sizeof(payload));
	ck_assert_int_eq(reliable_rx_put(&rx, tx.seq, 0, 0,
					 tx.length[0], 128,
					 payload, slots), 0);

	ck_assert_int_eq(reliable_tx_ack(&tx, 0x55), 1);
	ck_assert_int_eq(reliable_tx_next(&tx), 1);
	ck_assert_int_eq(reliable_tx_ack(&tx, 0xaa), 1);
	ck_assert_int_eq(reliable_tx_complete(&tx), 1);
}
END_TEST

START_TEST(test_out_of_order_gap)
{
	char fragment[] = "one fragment";
	char slots[3 * 4 * RELIABLE_MAX_FRAGS];
	struct reliable_rx rx;

	reliable_rx_reset(&rx, 1);
	ck_assert_int_eq(reliable_rx_put(&rx, 1, 2, 1, sizeof(fragment),
					 4 * RELIABLE_MAX_FRAGS,
					 fragment, slots), 1);
	ck_assert_int_eq(reliable_rx_complete(&rx), 0);
	ck_assert_int_eq(reliable_rx_put(&rx, 1, 0, 0, sizeof(fragment),
					 4 * RELIABLE_MAX_FRAGS,
					 fragment, slots), 1);
	ck_assert_int_eq(reliable_rx_complete(&rx), 0);
	ck_assert_int_eq(reliable_rx_put(&rx, 1, 1, 0, sizeof(fragment),
					 4 * RELIABLE_MAX_FRAGS,
					 fragment, slots), 1);
	ck_assert_int_eq(reliable_rx_complete(&rx), 1);
}
END_TEST

START_TEST(test_mtu_adaptation)
{
	struct reliable_mtu mtu;
	unsigned int before;
	unsigned int i;

	reliable_mtu_init(&mtu, 1200, 128, 1400);
	for (i = 0; i < 16; i++)
		reliable_mtu_success(&mtu, 1200);
	ck_assert_uint_gt(reliable_mtu_value(&mtu), 1200);

	before = reliable_mtu_value(&mtu);
	reliable_mtu_loss(&mtu);
	ck_assert_uint_lt(reliable_mtu_value(&mtu), before);

	for (i = 0; i < 64; i++)
		reliable_mtu_loss(&mtu);
	ck_assert_uint_eq(reliable_mtu_value(&mtu), 128);
}
END_TEST

TCase *
test_create(void)
{
	TCase *tc = tcase_create("Reliable");

	tcase_add_test(tc, test_control_bitmap);
	tcase_add_test(tc, test_selective_repeat);
	tcase_add_test(tc, test_out_of_order_gap);
	tcase_add_test(tc, test_mtu_adaptation);
	return tc;
}
