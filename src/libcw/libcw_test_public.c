/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "config.h"











#define _XOPEN_SOURCE 600 /* signaction() + SA_RESTART */


#include <sys/time.h>
#include <signal.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>


#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#include "libcw_test.h"
#include "libcw_debug.h"
#include "libcw_tq.h"
#include "libcw_utils.h"
#include "libcw_gen.h"



#ifdef LIBCW_UNIT_TESTS



typedef struct {
	int successes;
	int failures;
} cw_test_stats_t;


static cw_test_stats_t cw_stats_indep   = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_null    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_console = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_oss     = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_alsa    = { .successes = 0, .failures = 0 };
static cw_test_stats_t cw_stats_pa      = { .successes = 0, .failures = 0 };


static void cw_test_setup(void);
static int  cw_test_dependent(const char *audio_systems, const char *modules);
static int  cw_test_dependent_with(int audio_system, const char *modules, cw_test_stats_t *stats);
static void cw_test_print_stats(void);




static void cw_test_helper_tq_callback(void *data);





/* This variable will be used in "forever" test. This test function
   needs to open generator itself, so it needs to know the current
   audio system to be used. _NONE is just an initial value, to be
   changed in test setup. */
static int test_audio_system = CW_AUDIO_NONE;





/* Tone queue module. */
static void test_tone_queue_1(cw_test_stats_t *stats);
static void test_tone_queue_2(cw_test_stats_t *stats);
static void test_tone_queue_3(cw_test_stats_t *stats);
static void test_tone_queue_callback(cw_test_stats_t *stats);
/* Generator module. */
static void test_volume_functions(cw_test_stats_t *stats);
static void test_send_primitives(cw_test_stats_t *stats);
static void test_send_character_and_string(cw_test_stats_t *stats);
static void test_representations(cw_test_stats_t *stats);
/* Morse key module. */
static void test_keyer(cw_test_stats_t *stats);
static void test_straight_key(cw_test_stats_t *stats);
/* Other functions. */
static void test_parameter_ranges(cw_test_stats_t *stats);
static void test_cw_gen_forever_public(cw_test_stats_t *stats);

// static void cw_test_delayed_release(cw_test_stats_t *stats);





/*---------------------------------------------------------------------*/
/*  Unit tests                                                         */
/*---------------------------------------------------------------------*/





/**
   Notice that getters of parameter limits are tested in test_cw_get_x_limits()

   tests::cw_set_send_speed()
   tests::cw_get_send_speed()
   tests::cw_set_receive_speed()
   tests::cw_get_receive_speed()
   tests::cw_set_frequency()
   tests::cw_get_frequency()
   tests::cw_set_volume()
   tests::cw_get_volume()
   tests::cw_set_gap()
   tests::cw_get_gap()
   tests::cw_set_tolerance()
   tests::cw_get_tolerance()
   tests::cw_set_weighting()
   tests::cw_get_weighting()
*/
void test_parameter_ranges(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int txdot_usecs, txdash_usecs, end_of_element_usecs, end_of_character_usecs,
		end_of_word_usecs, additional_usecs, adjustment_usecs;

	/* Print default low level timing values. */
	cw_reset_send_receive_parameters();
	cw_get_send_parameters(&txdot_usecs, &txdash_usecs,
			       &end_of_element_usecs, &end_of_character_usecs,
			       &end_of_word_usecs, &additional_usecs,
			       &adjustment_usecs);
	printf("libcw: cw_get_send_parameters():\n"
	       "libcw:     %d, %d, %d, %d, %d, %d, %d\n",
	       txdot_usecs, txdash_usecs, end_of_element_usecs,
	       end_of_character_usecs,end_of_word_usecs, additional_usecs,
	       adjustment_usecs);


	/* Test setting and getting of some basic parameters. */

	struct {
		/* There are tree functions that take part in the
		   test: first gets range of acceptable values,
		   seconds sets a new value of parameter, and third
		   reads back the value. */

		void (* get_limits)(int *min, int *max);
		int (* set_new_value)(int new_value);
		int (* get_value)(void);

		int min; /* Minimal acceptable value of parameter. */
		int max; /* Maximal acceptable value of parameter. */

		const char *name;
	} test_data[] = {
		{ cw_get_speed_limits,      cw_set_send_speed,     cw_get_send_speed,     10000,  -10000,  "send_speed"    },
		{ cw_get_speed_limits,      cw_set_receive_speed,  cw_get_receive_speed,  10000,  -10000,  "receive_speed" },
		{ cw_get_frequency_limits,  cw_set_frequency,      cw_get_frequency,      10000,  -10000,  "frequency"     },
		{ cw_get_volume_limits,     cw_set_volume,         cw_get_volume,         10000,  -10000,  "volume"        },
		{ cw_get_gap_limits,        cw_set_gap,            cw_get_gap,            10000,  -10000,  "gap"           },
		{ cw_get_tolerance_limits,  cw_set_tolerance,      cw_get_tolerance,      10000,  -10000,  "tolerance"     },
		{ cw_get_weighting_limits,  cw_set_weighting,      cw_get_weighting,      10000,  -10000,  "weighting"     },
		{ NULL,                     NULL,                  NULL,                      0,       0,  NULL            }
	};


	for (int i = 0; test_data[i].get_limits; i++) {

		int status;
		bool failure;

		/* Get limits of values to be tested. */
		/* Notice that getters of parameter limits are tested
		   in test_cw_get_x_limits(). */
		test_data[i].get_limits(&test_data[i].min, &test_data[i].max);



		/* Test out-of-range value lower than minimum. */
		errno = 0;
		status = test_data[i].set_new_value(test_data[i].min - 1);
		failure = status || errno != EINVAL;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_set_%s(min - 1):", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test out-of-range value higher than maximum. */
		errno = 0;
		status = test_data[i].set_new_value(test_data[i].max + 1);
		failure = status || errno != EINVAL;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_set_%s(max + 1):", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test in-range values. */
		failure = false;
		for (int j = test_data[i].min; j <= test_data[i].max; j++) {
			test_data[i].set_new_value(j);
			if (test_data[i].get_value() != j) {
				failure = true;
				break;
			}
		}

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get/set_%s():", test_data[i].name);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		failure = false;
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Simple tests of queueing and dequeueing of tones

   Ensure we can generate a few simple tones, and wait for them to end.

   tests::cw_queue_tone()
   tests::cw_get_tone_queue_length()
   tests::cw_wait_for_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_1(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int l = 0;         /* Measured length of tone queue. */
	int expected = 0;  /* Expected length of tone queue. */

	int cw_min, cw_max;

	cw_set_volume(70);
	cw_get_frequency_limits(&cw_min, &cw_max);

	int N = 6;              /* Number of test tones put in queue. */
	int duration = 100000;  /* Duration of tone. */
	int delta_f = ((cw_max - cw_min) / (N - 1));      /* Delta of frequency in loops. */


	/* Test 1: enqueue N tones, and wait for each of them
	   separately. Control length of tone queue in the process. */

	/* Enqueue first tone. Don't check queue length yet.

	   The first tone is being dequeued right after enqueueing, so
	   checking the queue length would yield incorrect result.
	   Instead, enqueue the first tone, and during the process of
	   dequeueing it, enqueue rest of the tones in the loop,
	   together with checking length of the tone queue. */
	int f = cw_min;
	bool failure = !cw_queue_tone(duration, f);
	failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_queue_tone():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	/* This is to make sure that rest of tones is enqueued when
	   the first tone is being dequeued. */
	usleep(duration / 4);

	/* Enqueue rest of N tones. It is now safe to check length of
	   tone queue before and after queueing each tone: length of
	   the tone queue should increase (there won't be any decrease
	   due to dequeueing of first tone). */
	printf("libcw: enqueueing (1): \n");
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is filled - before
		   adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): pre:");
		// n = printf("libcw: cw_get_tone_queue_length(): pre-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		/* Add a tone to queue. All frequencies should be
		   within allowed range, so there should be no
		   error. */
		f = cw_min + i * delta_f;
		failure = !cw_queue_tone(duration, f);

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_queue_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is filled - after
		   adding a new tone. */
		l = cw_get_tone_queue_length();
		expected = (i - 1) + 1;
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): post:");
		// n = printf("libcw: cw_get_tone_queue_length(): post-queue: expected %d != result %d:", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Above we have queued N tones. libcw starts dequeueing first
	   of them before the last one is enqueued. This is why below
	   we should only check for N-1 of them. Additionally, let's
	   wait a moment till dequeueing of the first tone is without
	   a question in progress. */

	usleep(duration / 4);

	/* And this is the proper test - waiting for dequeueing tones. */
	printf("libcw: dequeueing (1):\n");
	for (int i = 1; i < N; i++) {

		/* Monitor length of a queue as it is emptied - before dequeueing. */
		l = cw_get_tone_queue_length();
		expected = N - i;
		//printf("libcw: cw_get_tone_queue_length(): pre:  l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): pre:");
		// n = printf("libcw: cw_get_tone_queue_length(): pre-dequeue:  expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Wait for each of N tones to be dequeued. */
		failure = !cw_wait_for_tone();

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_tone():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Monitor length of a queue as it is emptied - after dequeueing. */
		l = cw_get_tone_queue_length();
		expected = N - i - 1;
		//printf("libcw: cw_get_tone_queue_length(): post: l = %d\n", l);
		failure = l != expected;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length(): post:");
		// n = printf("libcw: cw_get_tone_queue_length(): post-dequeue: expected %d != result %d: failure\n", expected, l);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test 2: fill a queue, but this time don't wait for each
	   tone separately, but wait for a whole queue to become
	   empty. */
	failure = false;
	printf("libcw: enqueueing (2):\n");
	f = 0;
	for (int i = 0; i < N; i++) {
		f = cw_min + i * delta_f;
		if (!cw_queue_tone(duration, f)) {
			failure = true;
			break;
		}
	}

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_queue_tone(%08d, %04d):", duration, f);
	CW_TEST_PRINT_TEST_RESULT (failure, n);



	printf("libcw: dequeueing (2):\n");

	failure = !cw_wait_for_tone_queue();

	failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone_queue():");
	CW_TEST_PRINT_TEST_RESULT (failure, n);


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Run the complete range of tone generation, at 100Hz intervals,
   first up the octaves, and then down.  If the queue fills, though it
   shouldn't with this amount of data, then pause until it isn't so
   full.

   tests::cw_wait_for_tone()
   tests::cw_queue_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_2(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	cw_set_volume(70);
	int duration = 40000;

	int cw_min, cw_max;
	cw_get_frequency_limits(&cw_min, &cw_max);

	bool wait_failure = false;
	bool queue_failure = false;

	for (int i = cw_min; i < cw_max; i += 100) {
		while (cw_is_tone_queue_full()) {
			if (!cw_wait_for_tone()) {
				wait_failure = true;
				break;
			}
		}

		if (!cw_queue_tone(duration, i)) {
			queue_failure = true;
			break;
		}
	}

	for (int i = cw_max; i > cw_min; i -= 100) {
		while (cw_is_tone_queue_full()) {
			if (!cw_wait_for_tone()) {
				wait_failure = true;
				break;
			}
		}
		if (!cw_queue_tone(duration, i)) {
			queue_failure = true;
			break;
		}
	}


	queue_failure ? stats->failures++ : stats->successes++;
	int n = printf("libcw: cw_queue_tone():");
	CW_TEST_PRINT_TEST_RESULT (queue_failure, n);


	wait_failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone():");
	CW_TEST_PRINT_TEST_RESULT (wait_failure, n);


	bool wait_tq_failure = !cw_wait_for_tone_queue();
	wait_tq_failure ? stats->failures++ : stats->successes++;
	n = printf("libcw: cw_wait_for_tone_queue():");
	CW_TEST_PRINT_TEST_RESULT (wait_tq_failure, n);


	cw_queue_tone(0, 0);
	cw_wait_for_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Test the tone queue manipulations, ensuring that we can fill the
   queue, that it looks full when it is, and that we can flush it all
   again afterwards, and recover.

   tests::cw_get_tone_queue_capacity()
   tests::cw_get_tone_queue_length()
   tests::cw_queue_tone()
   tests::cw_wait_for_tone_queue()
*/
void test_tone_queue_3(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Small setup. */
	cw_set_volume(70);



	/* Test: properties (capacity and length) of empty tq. */
	{
		fprintf(stderr, "libcw:  --  initial test on empty tq:\n");

		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();
		cw_wait_for_tone_queue();

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_empty = cw_get_tone_queue_length();
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq empty: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: properties (capacity and length) of full tq. */

	/* FIXME: we call cw_queue_tone() until tq is full, and then
	   expect the tq to be full while we perform tests. Doesn't
	   the tq start dequeuing tones right away? Can we expect the
	   tq to be full for some time after adding last tone?
	   Hint: check when a length of tq is decreased. Probably
	   after playing first tone on tq, which - in this test - is
	   pretty long. Or perhaps not. */
	{
		fprintf(stderr, "libcw:  --  test on full tq:\n");

		int i = 0;
		/* FIXME: cw_is_tone_queue_full() is not tested */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(1000000, 100 + (i++ & 1) * 100);
		}

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		int len_full = cw_get_tone_queue_length();
		failure = len_full != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq full: %d %s %d:",
			   len_full, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: attempt to add tone to full queue. */
	{
		errno = 0;
		int status = cw_queue_tone(1000000, 100);
		bool failure = status || errno != EAGAIN;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_queue_tone() for full tq:");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: check again properties (capacity and length) of empty
	   tq after it has been in use.

	   Empty the tq, ensure that it is empty, and do the test. */
	{
		fprintf(stderr, "libcw:  --  final test on empty tq:\n");

		/* Empty tone queue and make sure that it is really
		   empty (wait for info from libcw). */
		cw_flush_tone_queue();
		cw_wait_for_tone_queue();

		int capacity = cw_get_tone_queue_capacity();
		bool failure = capacity != CW_TONE_QUEUE_CAPACITY_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_tone_queue_capacity(): %d %s %d:",
			       capacity, failure ? "!=" : "==", CW_TONE_QUEUE_CAPACITY_MAX);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Test that the tq is really empty after
		   cw_wait_for_tone_queue() has returned. */
		int len_empty = cw_get_tone_queue_length();
		failure = len_empty > 0;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_tone_queue_length() when tq empty: %d %s 0:", len_empty, failure ? "!=" : "==");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





static int cw_test_tone_queue_callback_data = 999999;
static int cw_test_helper_tq_callback_capture = false;


/**
   tests::cw_register_tone_queue_low_callback()
*/
void test_tone_queue_callback(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	for (int i = 1; i < 10; i++) {
		/* Test the callback mechanism for very small values,
		   but for a bit larger as well. */
		int level = i <= 5 ? i : 10 * i;

		int rv = cw_register_tone_queue_low_callback(cw_test_helper_tq_callback, (void *) &cw_test_tone_queue_callback_data, level);
		bool failure = rv == CW_FAILURE;
		sleep(1);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_register_tone_queue_low_callback(): %d:", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		/* Add a lot of tones to tone queue. "a lot" means three times more than a value of trigger level. */
		for (int j = 0; j < 3 * level; j++) {
			int duration = 10000;
			int f = 440;
			rv = cw_queue_tone(duration, f);
			assert (rv);
		}

		/* Allow the callback to work only after initial
		   filling of queue. */
		cw_test_helper_tq_callback_capture = true;

		/* Wait for the queue to be drained to zero. While the
		   tq is drained, and level of tq reaches trigger
		   level, a callback will be called. Its only task is
		   to copy the current level (tq level at time of
		   calling the callback) value into
		   cw_test_tone_queue_callback_data.

		   Since the value of trigger level is different in
		   consecutive iterations of loop, we can test the
		   callback for different values of trigger level. */
		cw_wait_for_tone_queue();

		/* Because of order of calling callback and decreasing
		   length of queue, I think that it's safe to assume
		   that there may be a difference of 1 between these
		   two values. */
		int diff = level - cw_test_tone_queue_callback_data;
		failure = diff > 1;

		failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: tone queue callback: %d", level);
		CW_TEST_PRINT_TEST_RESULT (failure, n);

		cw_reset_tone_queue();
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





static void cw_test_helper_tq_callback(void *data)
{
	if (cw_test_helper_tq_callback_capture) {
	int *d = (int *) data;
	*d = cw_get_tone_queue_length();

		cw_test_helper_tq_callback_capture = false;
	}

	return;
}





/**
   \brief Test control of volume

   Fill tone queue with short tones, then check that we can move the
   volume through its entire range.  Flush the queue when complete.

   tests::cw_get_volume_limits()
   tests::cw_set_volume()
   tests::cw_get_volume()
*/
void test_volume_functions(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int cw_min = -1, cw_max = -1;

	/* Test: get range of allowed volumes. */
	{
		cw_get_volume_limits(&cw_min, &cw_max);

		bool failure = cw_min != CW_VOLUME_MIN
			|| cw_max != CW_VOLUME_MAX;

		failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_get_volume_limits(): %d, %d", cw_min, cw_max);
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: decrease volume from max to low. */
	{
		/* Fill the tone queue with valid tones. */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_max; i >= cw_min; i -= 10) {
			cw_wait_for_tone();
			if (!cw_set_volume(i)) {
				set_failure = true;
				break;
			}

			if (cw_get_volume() != i) {
				get_failure = true;
				break;
			}

			cw_wait_for_tone();
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_set_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(stderr, "libcw: cw_get_volume() (down):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}



	/* Test: increase volume from zero to high. */
	{
		/* Fill tone queue with valid tones. */
		while (!cw_is_tone_queue_full()) {
			cw_queue_tone(100000, 440);
		}

		bool set_failure = false;
		bool get_failure = false;

		/* TODO: why call the cw_wait_for_tone() at the
		   beginning and end of loop's body? */
		for (int i = cw_min; i <= cw_max; i += 10) {
			cw_wait_for_tone();
			if (!cw_set_volume(i)) {
				set_failure = true;
				break;
			}

			if (cw_get_volume() != i) {
				get_failure = true;
				break;
			}
			cw_wait_for_tone();
		}

		set_failure ? stats->failures++ : stats->successes++;
		int n = fprintf(stderr, "libcw: cw_set_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (set_failure, n);

		get_failure ? stats->failures++ : stats->successes++;
		n = fprintf(stderr, "libcw: cw_get_volume() (up):");
		CW_TEST_PRINT_TEST_RESULT (get_failure, n);
	}

	cw_wait_for_tone();
	cw_flush_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}






/**
   \brief Test enqueueing and playing most basic elements of Morse code

   tests::cw_send_dot()
   tests::cw_send_dash()
   tests::cw_send_character_space()
   tests::cw_send_word_space()
*/
void test_send_primitives(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	int N = 20;

	/* Test: sending dot. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_dot()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_dot():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending dash. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_dash()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_dash():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending character space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_character_space()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_character_space():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending word space. */
	{
		bool failure = false;
		for (int i = 0; i < N; i++) {
			if (!cw_send_word_space()) {
				failure = true;
				break;
			}
		}
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_word_space():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   \brief Playing representations of characters

   tests::cw_send_representation()
   tests::cw_send_representation_partial()
*/
void test_representations(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);


	/* Test: sending valid representations. */
	{
		bool failure = !cw_send_representation(".-.-.-")
			|| !cw_send_representation(".-")
			|| !cw_send_representation("---")
			|| !cw_send_representation("...-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid representations. */
	{
		bool failure = cw_send_representation("INVALID")
			|| cw_send_representation("_._")
			|| cw_send_representation("-_-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending partial representation of a valid string. */
	{
		bool failure = !cw_send_representation_partial(".-.-.-");

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_representation_partial():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	cw_wait_for_tone_queue();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   Send all supported characters: first as individual characters, and then as a string.

   tests::cw_send_character()
   tests::cw_send_string()
*/
void test_send_character_and_string(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Test: sending all supported characters as individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		bool failure = false;

		/* Send all the characters from the charlist individually. */
		cw_list_characters(charlist);
		printf("libcw: cw_send_character(<valid>):\n"
		       "libcw:     ");
		for (int i = 0; charlist[i] != '\0'; i++) {
			putchar(charlist[i]);
			fflush(stdout);
			if (!cw_send_character(charlist[i])) {
				failure = true;
				break;
			}
			cw_wait_for_tone_queue();
		}

		putchar('\n');

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_character(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending invalid character. */
	{
		bool failure = cw_send_character(0);
		int n = printf("libcw: cw_send_character(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: sending all supported characters as single string. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		/* Send the complete charlist as a single string. */
		printf("libcw: cw_send_string(<valid>):\n"
		       "libcw:     %s\n", charlist);
		bool failure = !cw_send_string(charlist);

		while (cw_get_tone_queue_length() > 0) {
			printf("libcw: tone queue length %-6d\r", cw_get_tone_queue_length());
			fflush(stdout);
			cw_wait_for_tone();
		}
		printf("libcw: tone queue length %-6d\n", cw_get_tone_queue_length());
		cw_wait_for_tone_queue();

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_send_string(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	/* Test: sending invalid string. */
	{
		bool failure = cw_send_string("%INVALID%");
		int n = printf("libcw: cw_send_string(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   tests::cw_notify_keyer_paddle_event()
   tests::cw_wait_for_keyer_element()
   tests::cw_get_keyer_paddles()
*/
void test_keyer(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	/* Perform some tests on the iambic keyer.  The latch finer
	   timing points are not tested here, just the basics - dots,
	   dashes, and alternating dots and dashes. */

	int dot_paddle, dash_paddle;

	/* Test: keying dot. */
	{
		/* Seems like this function calls means "keyer pressed
		   until further notice". First argument is true, so
		   this is a dot. */
		bool failure = !cw_notify_keyer_paddle_event(true, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(true, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dot" paddle is pressed, get 30 "dot"
		   events from the keyer. */
		printf("libcw: testing iambic keyer dots   ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('.');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_keyer_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Second
		   argument is true, so this is a dash. */

		bool failure = !cw_notify_keyer_paddle_event(false, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(false, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);



		bool success = true;
		/* Since a "dash" paddle is pressed, get 30 "dash"
		   events from the keyer. */
		printf("libcw: testing iambic keyer dashes ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('-');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element():");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: keying alternate dit/dash. */
	{
		/* As above, it seems like this function calls means
		   "keyer pressed until further notice". Both
		   arguments are true, so both paddles are pressed at
		   the same time.*/
		bool failure = !cw_notify_keyer_paddle_event(true, true);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(true, true):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);


		bool success = true;
		printf("libcw: testing iambic alternating  ");
		fflush(stdout);
		for (int i = 0; i < 30; i++) {
			success = success && cw_wait_for_keyer_element();
			putchar('#');
			fflush(stdout);
		}
		putchar('\n');

		!success ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_wait_for_keyer_element:");
		CW_TEST_PRINT_TEST_RESULT (!success, n);
	}



	/* Test: preserving of paddle states. */
	{
		cw_get_keyer_paddles(&dot_paddle, &dash_paddle);
		bool failure = !dot_paddle || !dash_paddle;

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_get_keyer_paddles():");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}



	/* Test: set new state of paddles: no paddle pressed. */
	{
		bool failure = !cw_notify_keyer_paddle_event(false, false);

		failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_keyer_paddle_event(false, false):");
		CW_TEST_PRINT_TEST_RESULT (failure, n);
	}

	cw_wait_for_keyer();


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/**
   tests::cw_notify_straight_key_event()
   tests::cw_get_straight_key_state()
   tests::cw_is_straight_key_busy()
*/
void test_straight_key(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);

	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Not sure why, but we have N calls informing the
		   library that the key is not pressed.  TODO: why we
		   have N identical calls in a row? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}

			if (cw_get_straight_key_state()) {
				state_failure = true;
				break;
			}

			if (cw_is_straight_key_busy()) {
				busy_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_straight_key_busy():");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}



	{
		bool event_failure = false;
		bool state_failure = false;
		bool busy_failure = false;

		/* Again not sure why we have N identical calls in a
		   row. TODO: why? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_CLOSED)) {
				event_failure = true;
				break;
			}

			if (!cw_get_straight_key_state()) {
				state_failure = true;
				break;
			}

			if (!cw_is_straight_key_busy()) {
				busy_failure = true;
				break;
			}
		}


		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key closed>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);

		busy_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_straight_key_busy():");
		CW_TEST_PRINT_TEST_RESULT (busy_failure, n);
	}


	sleep(1);


	{
		bool event_failure = false;
		bool state_failure = false;

		/* Even more identical calls. TODO: why? */
		for (int i = 0; i < 10; i++) {
			if (!cw_notify_straight_key_event(CW_KEY_STATE_OPEN)) {
				event_failure = true;
				break;
			}
		}

		event_failure ? stats->failures++ : stats->successes++;
		int n = printf("libcw: cw_notify_straight_key_event(<key open>):");
		CW_TEST_PRINT_TEST_RESULT (event_failure, n);


		/* The key should be open, the function should return false. */
		int state = cw_get_straight_key_state();
		state_failure = state != CW_KEY_STATE_OPEN;

		state_failure ? stats->failures++ : stats->successes++;
		n = printf("libcw: cw_get_straight_key_state():");
		CW_TEST_PRINT_TEST_RESULT (state_failure, n);
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





# if 0
/*
 * cw_test_delayed_release()
 */
void cw_test_delayed_release(cw_test_stats_t *stats)
{
	printf("libcw: %s():\n", __func__);
	int failures = 0;
	struct timeval start, finish;
	int is_released, delay;

	/* This is slightly tricky to detect, but circumstantial
	   evidence is provided by SIGALRM disposition returning to SIG_DFL. */
	if (!cw_send_character_space()) {
		printf("libcw: ERROR: cw_send_character_space()\n");
		failures++;
	}

	if (gettimeofday(&start, NULL) != 0) {
		printf("libcw: WARNING: gettimeofday failed, test incomplete\n");
		return;
	}
	printf("libcw: waiting for cw_finalization delayed release");
	fflush(stdout);
	do {
		struct sigaction disposition;

		sleep(1);
		if (sigaction(SIGALRM, NULL, &disposition) != 0) {
			printf("libcw: WARNING: sigaction failed, test incomplete\n");
			return;
		}
		is_released = disposition.sa_handler == SIG_DFL;

		if (gettimeofday(&finish, NULL) != 0) {
			printf("libcw: WARNING: gettimeofday failed, test incomplete\n");
			return;
		}

		delay = (finish.tv_sec - start.tv_sec) * 1000000 + finish.tv_usec
			- start.tv_usec;
		putchar('.');
		fflush(stdout);
	}
	while (!is_released && delay < 20000000);
	putchar('\n');

	/* The release should be around 10 seconds after the end of
	   the sent space.  A timeout or two might leak in, reducing
	   it by a bit; we'll be ecstatic with more than five
	   seconds. */
	if (is_released) {
		printf("libcw: cw_finalization delayed release after %d usecs\n", delay);
		if (delay < 5000000) {
			printf("libcw: ERROR: cw_finalization release too quick\n");
			failures++;
		}
	} else {
		printf("libcw: ERROR: cw_finalization release wait timed out\n");
		failures++;
	}


	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/*
 * cw_test_signal_handling_callback()
 * cw_test_signal_handling()
 */
static int cw_test_signal_handling_callback_called = false;
void cw_test_signal_handling_callback(int signal_number)
{
	signal_number = 0;
	cw_test_signal_handling_callback_called = true;
}





void cw_test_signal_handling(cw_test_stats_t *stats)
{
	int failures = 0;
	struct sigaction action, disposition;

	/* Test registering, unregistering, and raising SIGUSR1.
	   SIG_IGN and handlers are tested, but not SIG_DFL, because
	   that stops the process. */
	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1,
                                   cw_test_signal_handling_callback)) {
		printf("libcw: ERROR: cw_register_signal_handler failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (!cw_test_signal_handling_callback_called) {
		printf("libcw: ERROR: cw_test_signal_handling_callback missed\n");
		failures++;
	}

	if (!cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf("libcw: ERROR: cw_register_signal_handler (overwrite) failed\n");
		failures++;
	}

	cw_test_signal_handling_callback_called = false;
	raise(SIGUSR1);
	sleep(1);
	if (cw_test_signal_handling_callback_called) {
		printf("libcw: ERROR: cw_test_signal_handling_callback called\n");
		failures++;
	}

	if (!cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler failed\n");
		failures++;
	}

	if (cw_unregister_signal_handler(SIGUSR1)) {
		printf("libcw: ERROR: cw_unregister_signal_handler invalid\n");
		failures++;
	}

	action.sa_handler = cw_test_signal_handling_callback;
	action.sa_flags = SA_RESTART;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGUSR1, &action, &disposition) != 0) {
		printf("libcw: WARNING: sigaction failed, test incomplete\n");
		return failures;
	}
	if (cw_register_signal_handler(SIGUSR1, SIG_IGN)) {
		printf("libcw: ERROR: cw_register_signal_handler clobbered\n");
		failures++;
	}
	if (sigaction(SIGUSR1, &disposition, NULL) != 0) {
		printf("libcw: WARNING: sigaction failed, test incomplete\n");
		return failures;
	}

	printf("libcw: cw_[un]register_signal_handler tests complete\n");
	return;
}
#endif





/*
  Version of test_cw_gen_forever() to be used in libcw_test_internal
  test executable.

  Because the function calls cw_generator_delete(), it should be
  executed as last test in test suite (unless you want to call
  cw_generator_new/start() again). */
void test_cw_gen_forever_public(cw_test_stats_t *stats)
{
	/* Make sure that an audio sink is closed. If we try to open
	   an OSS sink that is already open, we may end up with
	   "resource busy" error in libcw_oss.c module (that's what
	   happened on Alpine Linux).

	   Because of this call this test should be executed as last
	   one. */
	cw_generator_delete();

	int seconds = 5;
	printf("libcw: %s() (%d seconds):\n", __func__, seconds);

	unsigned int rv = test_cw_gen_forever_sub(seconds, test_audio_system, NULL);
	rv == 0 ? stats->successes++ : stats->failures++;

	CW_TEST_PRINT_FUNCTION_COMPLETED (__func__);

	return;
}





/*---------------------------------------------------------------------*/
/*  Unit tests drivers                                                 */
/*---------------------------------------------------------------------*/





/**
   \brief Set up common test conditions

   Run before each individual test, to handle setup of common test conditions.
*/
void cw_test_setup(void)
{
	cw_reset_send_receive_parameters();
	cw_set_send_speed(30);
	cw_set_receive_speed(30);
	cw_disable_adaptive_receive();
	cw_reset_receive_statistics();
	cw_unregister_signal_handler(SIGUSR1);
	errno = 0;

	return;
}





/* Tests that are dependent on a sound system being configured.
   Tone queue module functions */
static void (*const CW_TEST_FUNCTIONS_DEP_T[])(cw_test_stats_t *) = {
	test_tone_queue_1,
	test_tone_queue_2,
	test_tone_queue_3,
	test_tone_queue_callback,

	NULL
};


/* Tests that are dependent on a sound system being configured.
   Generator module functions. */
static void (*const CW_TEST_FUNCTIONS_DEP_G[])(cw_test_stats_t *) = {
	test_volume_functions,
	test_send_primitives,
	test_send_character_and_string,
	test_representations,

	NULL
};


/* Tests that are dependent on a sound system being configured.
   Morse key module functions */
static void (*const CW_TEST_FUNCTIONS_DEP_K[])(cw_test_stats_t *) = {
	test_keyer,
	test_straight_key,

	NULL
};


/* Tests that are dependent on a sound system being configured.
   Other modules' functions. */
static void (*const CW_TEST_FUNCTIONS_DEP_O[])(cw_test_stats_t *) = {
	test_parameter_ranges,
	test_cw_gen_forever_public,

	//cw_test_delayed_release,
	//cw_test_signal_handling, /* FIXME - not sure why this test fails :( */

	NULL
};





/**
   \brief Run tests for given audio system.

   Perform a series of self-tests on library public interfaces, using
   audio system specified with \p audio_system. Range of tests is specified
   with \p testset.

   \param audio_system - audio system to use for tests
   \param modules - libcw modules to be tested
   \param stats - test statistics

   \return -1 on failure to set up tests
   \return 0 if tests were run, and no errors occurred
   \return 1 if tests were run, and some errors occurred
*/
int cw_test_dependent_with(int audio_system, const char *modules, cw_test_stats_t *stats)
{
	test_audio_system = audio_system;

	int rv = cw_generator_new(audio_system, NULL);
	if (rv != 1) {
		fprintf(stderr, "libcw: can't create generator, stopping the test\n");
		return -1;
	}
	rv = cw_generator_start();
	if (rv != 1) {
		fprintf(stderr, "libcw: can't start generator, stopping the test\n");
		cw_generator_delete();
		return -1;
	}


	if (strstr(modules, "t")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_T[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_T[test])(stats);
		}
	}


	if (strstr(modules, "g")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_G[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_G[test])(stats);
		}
	}


	if (strstr(modules, "k")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_K[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_K[test])(stats);
		}
	}


	if (strstr(modules, "o")) {
		for (int test = 0; CW_TEST_FUNCTIONS_DEP_O[test]; test++) {
			cw_test_setup();
			(*CW_TEST_FUNCTIONS_DEP_O[test])(stats);
		}
	}


	sleep(1);
	cw_generator_stop();
	sleep(1);
	cw_generator_delete();

	/* All tests done; return success if no failures,
	   otherwise return an error status code. */
	return stats->failures ? 1 : 0;
}





/**
   \brief Run a series of tests for specified audio systems

   Function attempts to run a set of testcases for every audio system
   specified in \p audio_systems. These testcases require some kind
   of audio system configured. The function calls cw_test_dependent_with()
   to do the configuration and run the tests.

   \p audio_systems is a list of audio systems to be tested: "ncoap".
   Pass NULL pointer to attempt to test all of audio systems supported
   by libcw.

   \param audio_systems - list of audio systems to be tested
*/
int cw_test_dependent(const char *audio_systems, const char *modules)
{
	int n = 0, c = 0, o = 0, a = 0, p = 0;


	if (!audio_systems || strstr(audio_systems, "n")) {
		if (cw_is_null_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with null output\n");
			n = cw_test_dependent_with(CW_AUDIO_NULL, modules, &cw_stats_null);
		} else {
			fprintf(stderr, "libcw: null output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "c")) {
		if (cw_is_console_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with console output\n");
			c = cw_test_dependent_with(CW_AUDIO_CONSOLE, modules, &cw_stats_console);
		} else {
			fprintf(stderr, "libcw: console output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "o")) {
		if (cw_is_oss_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with OSS output\n");
			o = cw_test_dependent_with(CW_AUDIO_OSS, modules, &cw_stats_oss);
		} else {
			fprintf(stderr, "libcw: OSS output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "a")) {
		if (cw_is_alsa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with ALSA output\n");
			a = cw_test_dependent_with(CW_AUDIO_ALSA, modules, &cw_stats_alsa);
		} else {
			fprintf(stderr, "libcw: Alsa output not available\n");
		}
	}

	if (!audio_systems || strstr(audio_systems, "p")) {
		if (cw_is_pa_possible(NULL)) {
			fprintf(stderr, "========================================\n");
			fprintf(stderr, "libcw: testing with PulseAudio output\n");
			p = cw_test_dependent_with(CW_AUDIO_PA, modules, &cw_stats_pa);
		} else {
			fprintf(stderr, "libcw: PulseAudio output not available\n");
		}
	}

	if (!n && !c && !o && !a && !p) {
		return 0;
	} else {
		return -1;
	}
}





void cw_test_print_stats(void)
{
	printf("\n\nlibcw: Statistics of tests:\n\n");

	printf("libcw: Tests not requiring any audio system:            ");
	if (cw_stats_indep.failures + cw_stats_indep.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_indep.failures, cw_stats_indep.failures + cw_stats_indep.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with NULL audio system:          ");
	if (cw_stats_null.failures + cw_stats_null.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_null.failures, cw_stats_null.failures + cw_stats_null.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with console audio system:       ");
	if (cw_stats_console.failures + cw_stats_console.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_console.failures, cw_stats_console.failures + cw_stats_console.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with OSS audio system:           ");
	if (cw_stats_oss.failures + cw_stats_oss.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_oss.failures, cw_stats_oss.failures + cw_stats_oss.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with ALSA audio system:          ");
	if (cw_stats_alsa.failures + cw_stats_alsa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_alsa.failures, cw_stats_alsa.failures + cw_stats_alsa.successes);
	} else {
		printf("no tests were performed\n");
	}

	printf("libcw: Tests performed with PulseAudio audio system:    ");
	if (cw_stats_pa.failures + cw_stats_pa.successes) {
		printf("errors: %03d, total: %03d\n",
		       cw_stats_pa.failures, cw_stats_pa.failures + cw_stats_pa.successes);
	} else {
		printf("no tests were performed\n");
	}

	return;
}





#endif // #ifdef LIBCW_UNIT_TESTS





/**
   \return EXIT_SUCCESS if all tests complete successfully,
   \return EXIT_FAILURE otherwise
*/
int main(int argc, char *const argv[])
{
	int rv = 0;

#ifdef LIBCW_UNIT_TESTS
	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };

	unsigned int testset = 0;

	struct timeval seed;
	gettimeofday(&seed, NULL);
	// fprintf(stderr, "seed: %d\n", (int) seed.tv_usec);
	srand(seed.tv_usec);

	/* Obtain a bitmask of the tests to run from the command line
	   arguments. If none, then default to ~0, which effectively
	   requests all tests. */
	if (argc > 1) {
		testset = 0;
		for (int arg = 1; arg < argc; arg++) {
			unsigned int test = strtoul(argv[arg], NULL, 0);
			testset |= 1 << test;
		}
	} else {
		testset = ~0;
	}

#define CW_SYSTEMS_MAX 5
	char sound_systems[CW_SYSTEMS_MAX + 1];
#define CW_MODULES_MAX 4  /* g, t, k, o */
	char modules[CW_MODULES_MAX + 1];
	modules[0] = '\0';

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	atexit(cw_test_print_stats);

	/* Arrange for the test to exit on a range of signals. */
	for (int i = 0; SIGNALS[i] != 0; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, "libcw: ERROR: cw_register_signal_handler\n");
			exit(EXIT_FAILURE);
		}
	}

	rv = cw_test_dependent(sound_systems, modules);

#endif // #ifdef LIBCW_UNIT_TESTS

	return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
