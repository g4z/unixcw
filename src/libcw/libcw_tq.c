/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


/**
   \file libcw_tq.c

   \brief Queue of tones to be converted by generator to pcm data and
   sent to audio sink.

   Tone queue - a circular list of tone durations and frequencies pending,
   and a pair of indexes, tail (enqueue) and head (dequeue) to manage
   additions and asynchronous sending.

   The tone queue (the circular list) is implemented using constant
   size table.


   Explanation of "forever" tone:

   If a "forever" flag is set in a tone that is a last one on a tone
   queue, the tone should be constantly returned by dequeue function,
   without removing the tone - as long as it is a last tone on queue.

   Adding new, "non-forever" tone to the queue results in permanent
   dequeuing "forever" tone and proceeding to newly added tone.
   Adding new, "non-forever" tone ends generation of "forever" tone.

   The "forever" tone is useful for generating tones of length unknown
   in advance.

   dequeue() function recognizes the "forever" tone and acts as
   described above; there is no visible difference between dequeuing N
   separate "non-forever" tones of length L [us], and dequeuing a
   "forever" tone of length L [us] N times in a row.

   Because of some corner cases related to "forever" tones it is very
   strongly advised to set "low water mark" level to no less than 2
   tones.
*/





#include <inttypes.h> /* "PRIu32" */
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h> /* SIGALRM */

#include "libcw.h"
#include "libcw_tq.h"
#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_signal.h"



#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif



/*
   The CW tone queue functions implement the following state graph:

                              (queue empty)
            +-----------------------------------------------------+
            |                                                     |
            |                                                     |
            |        (tone(s) added to queue,                     |
            v        dequeueing process started)                  |
   ----> CW_TQ_IDLE -------------------------------> CW_TQ_BUSY --+
                                                 ^        |
                                                 |        |
                                                 +--------+
                                             (queue not empty)


   Above diagram shows two states of a queue, but dequeue function
   returns three distinct values: CW_TQ_DEQUEUED,
   CW_TQ_NDEQUEUED_EMPTY, CW_TQ_NDEQUEUED_IDLE. Having these three
   values is important for the function that calls the dequeue
   function. If you ever intend to limit number of return values of
   dequeue function to two, you will also have to re-think how
   cw_gen_dequeue_and_play_internal() operates.

   Future libcw API should (completely) hide tone queue from client
   code. The client code should only operate on a generator - enqueue
   tones to generator, flush a generator, register low water callback
   with generator etc. There is very little (or even no) need to
   explicitly reveal to client code this implementation detail called
   "tone queue".
*/





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





static int      cw_tq_set_capacity_internal(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark);
static uint32_t cw_tq_get_high_water_mark_internal(cw_tone_queue_t *tq) __attribute__((unused));
static uint32_t cw_tq_prev_index_internal(cw_tone_queue_t *tq, uint32_t current) __attribute__((unused));
static uint32_t cw_tq_next_index_internal(cw_tone_queue_t *tq, uint32_t current);
static int      cw_tq_dequeue_sub_internal(cw_tone_queue_t *tq, cw_tone_t *tone);




/* Not used anymore. 2015.02.22. */
#if 0
/* Remember that tail and head are of unsigned type.  Make sure that
   order of calculations is correct when tail < head. */
#define CW_TONE_QUEUE_LENGTH(m_tq)				\
	( m_tq->tail >= m_tq->head				\
	  ? m_tq->tail - m_tq->head				\
	  : m_tq->capacity - m_tq->head + m_tq->tail)		\

#endif





/**
   \brief Create new tone queue

   Allocate and initialize new tone queue structure.

   testedin::test_cw_tone_queue_init_internal()

   \return pointer to new tone queue on success
   \return NULL pointer on failure
*/
cw_tone_queue_t *cw_tq_new_internal(void)
{
	cw_tone_queue_t *tq = (cw_tone_queue_t *) malloc(sizeof (cw_tone_queue_t));
	if (!tq) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
				      "libcw/tq: failed to malloc() tone queue");
		return (cw_tone_queue_t *) NULL;
	}

	int rv = pthread_mutex_init(&(tq->mutex), NULL);
	cw_assert (!rv, "failed to initialize mutex");

	pthread_mutex_lock(&(tq->mutex));

	tq->tail = 0;
	tq->head = 0;
	tq->len = 0;
	tq->state = CW_TQ_IDLE;

	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;
	tq->call_callback = false;

	tq->gen = (cw_gen_t *) NULL;


	rv = cw_tq_set_capacity_internal(tq, CW_TONE_QUEUE_CAPACITY_MAX, CW_TONE_QUEUE_HIGH_WATER_MARK_MAX);
	cw_assert (rv, "failed to set initial capacity of tq");

	pthread_mutex_unlock(&(tq->mutex));

	return tq;
}





void cw_tq_delete_internal(cw_tone_queue_t **tq)
{
	cw_assert (tq, "pointer to tq is NULL");

	if (!tq || !*tq) {
		return;
	}

	free(*tq);
	*tq = (cw_tone_queue_t *) NULL;

	return;
}





/**
   \brief Set capacity and high water mark for queue

   Set two parameters of queue: total capacity of the queue, and high
   water mark. When calling the function, client code must provide
   valid values of both parameters.

   Calling the function *by a client code* for a queue is optional, as
   a queue has these parameters always set to default values
   (CW_TONE_QUEUE_CAPACITY_MAX and CW_TONE_QUEUE_HIGH_WATER_MARK_MAX)
   by internal call to cw_tq_new_internal().

   \p capacity must be no larger than CW_TONE_QUEUE_CAPACITY_MAX.
   \p high_water_mark must be no larger than  CW_TONE_QUEUE_HIGH_WATER_MARK_MAX.

   Both values must be larger than zero (this condition is subject to
   changes in future revisions of the library).

   \p high_water_mark must be no larger than \p capacity.

   Functions set errno to EINVAL if any of the two parameters is invalid.

   testedin::test_cw_tq_capacity_test_init()

   \param tq - tone queue to configure
   \param capacity - new capacity of queue
   \param high_water_mark - high water mark for the queue

   \return CW_SUCCESS on success, CW_FAILURE otherwise
*/
int cw_tq_set_capacity_internal(cw_tone_queue_t *tq, uint32_t capacity, uint32_t high_water_mark)
{
	cw_assert (tq, "tq is NULL");
	if (!tq) {
		return CW_FAILURE;
	}

	if (!high_water_mark || high_water_mark > CW_TONE_QUEUE_HIGH_WATER_MARK_MAX) {
		/* If we allowed high water mark to be zero, the queue
		   would not accept any new tones: it would constantly
		   be full. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (!capacity || capacity > CW_TONE_QUEUE_CAPACITY_MAX) {
		/* Tone queue of capacity zero doesn't make much
		   sense, so capacity == 0 is not allowed. */
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (high_water_mark > capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	tq->capacity = capacity;
	tq->high_water_mark = high_water_mark;

	return CW_SUCCESS;
}





/**
   \brief Return capacity of a queue

   testedin::test_cw_tq_get_capacity_internal()

   \param tq - tone queue, for which you want to get capacity

   \return capacity of tone queue
*/
uint32_t cw_tq_get_capacity_internal(cw_tone_queue_t *tq)
{
	cw_assert (tq, "tone queue is NULL");
	return tq->capacity;
}





/**
   \brief Return high water mark of a queue

   \param tq - tone queue, for which you want to get high water mark

   \return high water mark of tone queue
*/
uint32_t cw_tq_get_high_water_mark_internal(cw_tone_queue_t *tq)
{
	cw_assert (tq, "tone queue is NULL");

	return tq->high_water_mark;
}





/**
   \brief Return number of items on tone queue

   testedin::test_cw_tq_length_internal()

   \param tq - tone queue

   \return the count of tones currently held in the circular tone buffer.
*/
uint32_t cw_tq_length_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&tq->mutex);
	size_t len = tq->len;
	pthread_mutex_unlock(&tq->mutex);

	return len;
}





/**
   \brief Get previous index to queue

   Calculate index of previous element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tq_prev_index_internal()

   \param tq - tone queue for which to calculate index
   \param ind - index in relation to which to calculate index of previous element in queue

   \return index of previous element in queue
*/
uint32_t cw_tq_prev_index_internal(cw_tone_queue_t *tq, uint32_t ind)
{
	return ind == 0 ? tq->capacity - 1 : ind - 1;
}





/**
   \brief Get next index to queue

   Calculate index of next element in queue, relative to given \p ind.
   The function calculates the index taking circular wrapping into
   consideration.

   testedin::test_cw_tq_next_index_internal()

   \param tq - tone queue for which to calculate index
   \param ind - index in relation to which to calculate index of next element in queue

   \return index of next element in queue
*/
uint32_t cw_tq_next_index_internal(cw_tone_queue_t *tq, uint32_t ind)
{
	return ind == tq->capacity - 1 ? 0 : ind + 1;
}





/**
   \brief Dequeue a tone from tone queue

   Dequeue a tone from tone queue.

   The queue returns three distinct values. This may seem overly
   complicated for a tone queue, but it actually works. The way the
   generator interacts with the tone queue, and the way the enqueueing
   works, depend on the dequeue function to return three values. If
   you ever try to make the dequeue function return two values, you
   would also have to redesign parts of generator and of enqueueing
   code.

   Look in cw_gen_write_to_soundcard_internal(). The function makes
   decision based on two distinct tone queue states (described by
   CW_TQ_DEQUEUED or CW_TQ_NDEQUEUED_EMPTY). So the _write() function
   must be executed by generator for both return values. But we also
   need a third return value that will tell the generator not to
   execute _write() *at all*, but just wait for signal. This third
   value is CW_TQ_NDEQUEUED_IDLE.

   These three return values are:

   \li CW_TQ_DEQUEUED - dequeue() function successfully dequeues and
       returns through \p tone a valid tone. dequeue() understands how
       "forever" tone should be handled: if such tone is the last tone
       on the queue, the function actually both returns the "forever"
       tone, and keeps it in queue (until next tone is enqueued).

   \li CW_TQ_NDEQUEUED_EMPTY - dequeue() function can't dequeue a tone
       from tone queue, because the queue has been just emptied, i.e.
       previous call to dequeue() was successful and returned
       CW_TQ_DEQUEUED, but that was the last tone on queue. This
       return value is a way of telling client code "I've had tones,
       but no more, you should probably stop playing any sounds and
       become silent". If no new tones are enqueued, the next call to
       dequeue() will return CW_TQ_NDEQUEUED_IDLE.

   \li CW_TQ_NDEQUEUED_IDLE - dequeue() function can't dequeue a tone
       from tone queue, because the queue is empty, and the tone queue
       has no memory of being non-empty before. This is the value that
       dequeue() would return for brand new tone queue. This is also
       value returned by dequeue() when its previous return value was
       CW_TQ_NDEQUEUED_EMPTY, and no new tones were enqueued since
       then.

   Notice that returned value does not describe internal state of tone
   queue.

   Successfully dequeued tone is returned through function's argument
   \p tone. The function does not modify the arguments if there are no
   tones to dequeue (CW_TQ_NDEQUEUED_EMPTY, CW_TQ_NDEQUEUED_IDLE).

   As mentioned above, dequeue() understands how "forever" tone works.
   If the last tone in queue has "forever" flag set, the function
   won't permanently dequeue it. Instead, it will keep returning
   (through \p tone) the tone on every call, until a new tone is added
   to the queue after the "forever" tone.

   testedin::test_cw_tq_dequeue_internal()
   testedin::test_cw_tq_test_capacity_2()

   \param tq - tone queue
   \param tone - dequeued tone

   \return CW_TQ_DEQUEUED (see information above)
   \return CW_TQ_NDEQUEUED_EMPTY (see information above)
   \return CW_TQ_NDEQUEUED_IDLE (see information above)
*/
int cw_tq_dequeue_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	pthread_mutex_lock(&(tq->mutex));

	/* Decide what to do based on the current state. */
	switch (tq->state) {

	case CW_TQ_IDLE:
		pthread_mutex_unlock(&(tq->mutex));
		/* Ignore calls if our state is idle. */
		return CW_TQ_NDEQUEUED_IDLE;

	case CW_TQ_BUSY:
		/* If there are some tones in queue, dequeue the next
		   tone. If there are no more tones, go to the idle state. */
		if (tq->len) {
			bool call_callback = cw_tq_dequeue_sub_internal(tq, tone);

			/* Notify the key control function about current tone. */
			if (tq->gen && tq->gen->key) {
				cw_key_tk_set_value_internal(tq->gen->key, tone->frequency ? CW_KEY_STATE_CLOSED : CW_KEY_STATE_OPEN);
			}

			pthread_mutex_unlock(&(tq->mutex));

			/* Since client's callback can use functions
			   that call libcw's pthread_mutex_lock(), we
			   should put the callback *after* we release
			   pthread_mutex_unlock() in this function. */

			if (call_callback) {
				(*(tq->low_water_callback))(tq->low_water_callback_arg);
			}

			return CW_TQ_DEQUEUED;
		} else { /* tq->len == 0 */
			/* State of tone queue is still "busy", but
			   there are no tones left on the queue.

			   Time to bring tq->state in sync with
			   tq->len. Set state to idle, indicating that
			   dequeuing has finished for the moment. */
			tq->state = CW_TQ_IDLE;

			/* There is no tone to dequeue, so don't
			   modify function's arguments. Client code
			   will learn about "no valid tone returned
			   through function argument" state through
			   return value. */

			/* Notify the key control function about current tone. */
			if (tq->gen && tq->gen->key) {
				cw_key_tk_set_value_internal(tq->gen->key, CW_KEY_STATE_OPEN);
			}

			pthread_mutex_unlock(&(tq->mutex));
			return CW_TQ_NDEQUEUED_EMPTY;
		}
	}

	pthread_mutex_unlock(&(tq->mutex));
	/* will never get here as "queue state" enum has only two values */
	cw_assert(0, "reached unreachable place");
	return CW_TQ_NDEQUEUED_IDLE;
}





/**
   \brief Handle dequeueing of tone from non-empty tone queue

   Function gets a tone from head of the queue.

   If this was a last tone in queue, and it was a "forever" tone, the
   tone is not removed from the queue (the philosophy of "forever"
   tone), and "low watermark" condition is not checked.

   Otherwise remove the tone from tone queue, check "low watermark"
   condition, and return value of the check (true/false).

   In any case, dequeued tone is returned through \p tone. \p tone
   must be a valid pointer provided by caller.

   \param tq - tone queue
   \param tone - dequeued tone (output from the function)

   \return true if a condition for calling "low watermark" callback is true
   \return false otherwise
*/
int cw_tq_dequeue_sub_internal(cw_tone_queue_t *tq, /* out */ cw_tone_t *tone)
{
	CW_TONE_COPY(tone, &(tq->queue[tq->head]));

	if (tone->forever && tq->len == 1) {
		/* Don't permanently remove the last tone that is
		   "forever" tone in queue. Keep it in tq until client
		   code adds next tone (possibly forever). Queue's
		   head should not be iterated. "forever" tone should
		   be played by caller code, this is why we return the
		   tone through function's argument. */

		/* Don't call "low watermark" callback for "forever"
		   tone. As the comment in this function below has
		   stated: avoid endlessly calling the callback if the
		   only queued tone is "forever" tone.*/
		return false;
	}

	/* Used to check if we passed tq's low level watermark. */
	uint32_t tq_len_before = tq->len;

	/* Dequeue. We already have the tone, now update tq's state. */
	tq->head = cw_tq_next_index_internal(tq, tq->head);
	tq->len--;


	if (tq->len == 0) {
		/* Verify basic property of empty tq. */
		cw_assert (tq->head == tq->tail, "Head: %"PRIu32", tail: %"PRIu32"", tq->head, tq->tail);
	}


#if 0 /* Disabled because these debug messages produce lots of output
	 to console. Enable only when necessary. */
	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw/tq: dequeue tone %d us, %d Hz", tone->len, tone->frequency);
	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw/tq: head = %"PRIu32", tail = %"PRIu32", length = %"PRIu32" -> %"PRIu32"",
		      tq->head, tq->tail, tq_len_before, tq->len);
#endif

	/* You can remove this assert in future. It is only temporary,
	   to check that some changes introduced on 2015.03.01 didn't
	   break one assumption. */
	cw_assert (!(tone->forever && tq_len_before == 1), "\"forever\" tone appears!");


	bool call_callback = false;
	if (tq->low_water_callback) {
		/* It may seem that the double condition in 'if ()' is
		   redundant, but for some reason it is necessary. Be
		   very, very careful when modifying this. */
		if (tq_len_before > tq->low_water_mark
		    && tq->len <= tq->low_water_mark) {

			call_callback = true;
		}
	}

	return call_callback;
}





/**
   \brief Add tone to tone queue

   Enqueue a tone for specified frequency and number of microseconds.
   This routine adds the new tone to the queue, and if necessary sends
   signal to generator, so that the generator can dequeue the tone.

   The routine returns CW_SUCCESS on success. If the tone queue is
   full, the routine returns CW_FAILURE, with errno set to EAGAIN.  If
   the iambic keyer or straight key are currently busy, the routine
   returns CW_FAILURE, with errno set to EBUSY.

   The function does not accept tones with frequency outside of
   CW_FREQUENCY_MIN-CW_FREQUENCY_MAX range.

   If length of a tone (tone->len) is zero, the function does not
   add it to tone queue and returns CW_SUCCESS.

   The function does not accept tones with negative values of len.

   testedin::test_cw_tq_enqueue_internal_1()
   testedin::test_cw_tq_enqueue_internal_2()
   testedin::test_cw_tq_test_capacity_1()
   testedin::test_cw_tq_test_capacity_2()

   \param tq - tone queue
   \param tone - tone to enqueue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_enqueue_internal(cw_tone_queue_t *tq, cw_tone_t *tone)
{
	/* Check the arguments given for realistic values. */
	if (tone->frequency < CW_FREQUENCY_MIN
	    || tone->frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len < 0) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	if (tone->len == 0) {
		/* Drop empty tone. It won't be played anyway, and for
		   now there are no other good reasons to enqueue
		   it. While it may happen in higher-level code to
		   create such tone, but there is no need to spend
		   time on it here. */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
			      "libcw/tq: dropped tone with len == 0");
		return CW_SUCCESS;
	}

#if 0 /* This part is no longer in use. It seems that it's not necessary. 2015.02.22. */
	/* If the keyer or straight key are busy, return an error.
	   This is because they use the sound card/console tones and key
	   control, and will interfere with us if we try to use them at
	   the same time. */
	if (cw_key_ik_is_busy_internal(tq->gen->key) || cw_key_sk_is_busy_internal(tq->gen->key)) {
		errno = EBUSY;
		return CW_FAILURE;
	}
#endif

	pthread_mutex_lock(&(tq->mutex));

	if (tq->len == tq->capacity) {
		/* Tone queue is full. */

		errno = EAGAIN;
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_ERROR,
			      "libcw/tq: can't enqueue tone, tq is full");
		pthread_mutex_unlock(&(tq->mutex));

		return CW_FAILURE;
	}


	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
		      "libcw/tq: enqueue tone %d us, %d Hz", tone->len, tone->frequency);

	/* Enqueue the new tone.

	   Notice that tail is incremented after adding a tone. This
	   means that for empty tq new tone is inserted at index
	   tail == head (which should be kind of obvious). */
	CW_TONE_COPY(&(tq->queue[tq->tail]), tone);

	tq->tail = cw_tq_next_index_internal(tq, tq->tail);
	tq->len++;


	if (tq->state == CW_TQ_IDLE) {
		/* A loop in cw_gen_dequeue_and_play_internal()
		   function may await for the queue to be filled with
		   new tones to dequeue and play.  It waits for a
		   signal, for information that there are some new
		   tones in tone queue. This is a right place and time
		   to send such a signal. */
		tq->state = CW_TQ_BUSY;
		pthread_kill(tq->gen->thread.id, SIGALRM);
	}

	pthread_mutex_unlock(&(tq->mutex));
	return CW_SUCCESS;
}





/**
   \brief Register callback for low queue state

   Register a function to be called automatically by the dequeue routine
   whenever the tone queue falls to a given \p level. To be more precise:
   the callback is called by queue manager if, after dequeueing a tone,
   the manager notices that tone queue length has become equal or less
   than \p level.

   \p callback_arg may be used to give a value passed back on callback
   calls.  A NULL function pointer suppresses callbacks.  On success,
   the routine returns CW_SUCCESS.

   If \p level is invalid, the routine returns CW_FAILURE with errno set to
   EINVAL.  Any callback supplied will be called in signal handler context.

   \param tq - tone queue
   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_tq_register_low_level_callback_internal(cw_tone_queue_t *tq, void (*callback_func)(void*), void *callback_arg, int level)
{
	if (level < 0 || (uint32_t) level >= tq->capacity) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Store the function and low water mark level. */
	tq->low_water_mark = (uint32_t) level;
	tq->low_water_callback = callback_func;
	tq->low_water_callback_arg = callback_arg;

	return CW_SUCCESS;
}





/**
   \brief Check if tone sender is busy

   Indicate if the tone sender is busy.

   \param tq - tone queue

   \return true if there are still entries in the tone queue
   \return false if the queue is empty
*/
bool cw_tq_is_busy_internal(cw_tone_queue_t *tq)
{
	return tq->state != CW_TQ_IDLE;
}





/**
   \brief Wait for the current tone to complete

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns CW_FAILURE, with errno set to EDEADLK,
   to avoid indefinite waits.

   \param tq - tone queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_tone_internal(cw_tone_queue_t *tq)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait for the head index to change or the dequeue to go idle. */
	uint32_t check_tq_head = tq->head;
	while (tq->head == check_tq_head && tq->state != CW_TQ_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain

   The routine returns CW_SUCCESS on success. If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK,
   to avoid indefinite waits.

   \param tq - tone queue

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_tone_queue_internal(cw_tone_queue_t *tq)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the dequeue indicates it has hit the end of the queue. */
	while (tq->state != CW_TQ_IDLE) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This routine is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK, to
   avoid indefinite waits.

   \param tq - tone queue
   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_tq_wait_for_level_internal(cw_tone_queue_t *tq, uint32_t level)
{
	if (cw_sigalrm_is_blocked_internal()) {
		/* no point in waiting for event, when signal
		   controlling the event is blocked */
		errno = EDEADLK;
		return CW_FAILURE;
	}

	/* Wait until the queue length is at or below criticality. */
	while (cw_tq_length_internal(tq) > level) {
		cw_signal_wait_internal();
	}

	return CW_SUCCESS;
}





/**
   \brief Indicate if the tone queue is full

   This is a helper subroutine created so that I can pass a test tone
   queue in unit tests. The 'cw_is_tone_queue_full() works only on
   default tone queue object.

   testedin::test_cw_tq_is_full_internal()

   \param tq - tone queue to check

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_tq_is_full_internal(cw_tone_queue_t *tq)
{
	return tq->len == tq->capacity;
}





void cw_tq_reset_internal(cw_tone_queue_t *tq)
{
	/* Empty and reset the queue, and force state to idle. */
	tq->len = 0;
	tq->head = tq->tail;
	tq->state = CW_TQ_IDLE;

	/* Reset low water mark details to their initial values. */
	tq->low_water_mark = 0;
	tq->low_water_callback = NULL;
	tq->low_water_callback_arg = NULL;

	return;
}





void cw_tq_flush_internal(cw_tone_queue_t *tq)
{
	pthread_mutex_lock(&(tq->mutex));

	/* Empty and reset the queue. */
	tq->len = 0;
	tq->head = tq->tail;

	pthread_mutex_unlock(&(tq->mutex));

	/* If we can, wait until the dequeue goes idle. */
	if (!cw_sigalrm_is_blocked_internal()) {
		cw_tq_wait_for_tone_queue_internal(tq);
	}

	return;
}





/* *** Unit tests *** */





#ifdef LIBCW_UNIT_TESTS


#include "libcw_test.h"

static cw_tone_queue_t *test_cw_tq_capacity_test_init(uint32_t capacity, uint32_t high_water_mark, int head_shift);
static unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq);
static unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq);





/**
   tests::cw_tq_new_internal()
   tests::cw_tq_delete_internal()
*/
unsigned int test_cw_tq_new_delete_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_new/delete_internal():");

	/* Arbitrary number of calls to new()/delete() pair. */
	for (int i = 0; i < 40; i++) {
		cw_tone_queue_t *tq = cw_tq_new_internal();
		cw_assert (tq, "failed to initialize tone queue");

		/* Try to access some fields in cw_tone_queue_t just
		   to be sure that the tq has been allocated
		   properly. */
		cw_assert (tq->head == 0, "head in new tone queue is not at zero");
		tq->tail = tq->head + 10;
		cw_assert (tq->tail == 10, "tail didn't store correct new value");

		cw_tq_delete_internal(&tq);
		cw_assert (tq == NULL, "delete() didn't set the pointer to NULL");
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tq_get_capacity_internal()
*/
unsigned int test_cw_tq_get_capacity_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_get_capacity_internal():");

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to initialize tone queue");
	for (uint32_t i = 10; i < 40; i++) {
		/* This is a silly test, but let's have any test of
		   the getter. */

		tq->capacity = i;
		uint32_t capacity = cw_tq_get_capacity_internal(tq);
		cw_assert (capacity == i, "incorrect capacity: %"PRIu32" != %"PRIu32"", capacity, i);
	}

	cw_tq_delete_internal(&tq);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tq_prev_index_internal()
*/
unsigned int test_cw_tq_prev_index_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_prev_index_internal():");

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");

	struct {
		int arg;
		uint32_t expected;
		bool guard;
	} input[] = {
		{ tq->capacity - 4, tq->capacity - 5, false },
		{ tq->capacity - 3, tq->capacity - 4, false },
		{ tq->capacity - 2, tq->capacity - 3, false },
		{ tq->capacity - 1, tq->capacity - 2, false },

		/* This one should never happen. We can't pass index
		   equal "capacity" because it's out of range. */
		/*
		{ tq->capacity - 0, tq->capacity - 1, false },
		*/

		{                0, tq->capacity - 1, false },
		{                1,                0, false },
		{                2,                1, false },
		{                3,                2, false },
		{                4,                3, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	while (!input[i].guard) {
		uint32_t prev = cw_tq_prev_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) prev, input[i].expected);
		cw_assert (prev == input[i].expected,
			   "calculated \"prev\" != expected \"prev\": %"PRIu32" != %"PRIu32"",
			   prev, input[i].expected);
		i++;
	}

	cw_tq_delete_internal(&tq);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tq_next_index_internal()
*/
unsigned int test_cw_tq_next_index_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_next_index_internal():");

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");

	struct {
		int arg;
		uint32_t expected;
		bool guard;
	} input[] = {
		{ tq->capacity - 5, tq->capacity - 4, false },
		{ tq->capacity - 4, tq->capacity - 3, false },
		{ tq->capacity - 3, tq->capacity - 2, false },
		{ tq->capacity - 2, tq->capacity - 1, false },
		{ tq->capacity - 1,                0, false },
		{                0,                1, false },
		{                1,                2, false },
		{                2,                3, false },
		{                3,                4, false },

		{                0,                0, true  } /* guard */
	};

	int i = 0;
	while (!input[i].guard) {
		uint32_t next = cw_tq_next_index_internal(tq, input[i].arg);
		//fprintf(stderr, "arg = %d, result = %d, expected = %d\n", input[i].arg, (int) next, input[i].expected);
		cw_assert (next == input[i].expected,
			   "calculated \"next\" != expected \"next\": %"PRIu32" != %"PRIu32"",
			   next, input[i].expected);
		i++;
	}

	cw_tq_delete_internal(&tq);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_length_internal()
   tests::cw_get_tone_queue_length()
*/
unsigned int test_cw_tq_length_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_length_internal():");

	/* This is just some code copied from implementation of
	   'enqueue' function. I don't use 'enqueue' function itself
	   because it's not tested yet. I get rid of all the other
	   code from the 'enqueue' function and use only the essential
	   part to manually add elements to list, and then to check
	   length of the list. */

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	for (uint32_t i = 0; i < tq->capacity; i++) {

		/* This block of code pretends to be enqueue function.
		   The most important functionality of enqueue
		   function is done here manually. We don't do any
		   checks of boundaries of tq, we trust that this is
		   enforced by for loop's conditions. */
		{
			/* Notice that this is *before* enqueueing the tone. */
			cw_assert (tq->len < tq->capacity,
				   "length before enqueue reached capacity: %"PRIu32" / %"PRIu32"",
				   tq->len, tq->capacity);

			/* Enqueue the new tone and set the new tail index. */
			CW_TONE_COPY(&(tq->queue[tq->tail]), &tone);
			tq->tail = cw_tq_next_index_internal(tq, tq->tail);
			tq->len++;

			cw_assert (tq->len <= tq->capacity,
				   "length after enqueue exceeded capacity: %"PRIu32" / %"PRIu32"",
				   tq->len, tq->capacity);
		}


		/* OK, added a tone, ready to measure length of the queue. */
		uint32_t len = cw_tq_length_internal(tq);
		cw_assert (len == i + 1, "after adding tone #%"PRIu32" length is incorrect (%"PRIu32")", i, len);
		cw_assert (len == tq->len, "lengths don't match: %"PRIu32" != %"PRIu32"", len, tq->len);
	}

	cw_tq_delete_internal(&tq);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
  \brief Wrapper for tests of enqueue() and dequeue() function

  First we fill a tone queue when testing enqueue(), and then use the
  tone queue to test dequeue().
*/
unsigned int test_cw_tq_enqueue_dequeue_internal(void)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");
	tq->state = CW_TQ_BUSY;

	/* Fill the tone queue with tones. */
	unsigned int rv = test_cw_tq_enqueue_internal_1(tq);
	cw_assert (rv == 0, "test of enqueue() failed");

	/* Use the same (now filled) tone queue to test dequeue()
	   function. */
        rv = test_cw_tq_dequeue_internal(tq);
	cw_assert (rv == 0, "test of dequeue() failed");

	cw_tq_delete_internal(&tq);

	return 0;
}





/**
   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_1(cw_tone_queue_t *tq)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_enqueue_internal():");

	/* At this point cw_tq_length_internal() should be
	   tested, so we can use it to verify correctness of 'enqueue'
	   function. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	for (uint32_t i = 0; i < tq->capacity; i++) {

		/* This tests for potential problems with function call. */
		int rv = cw_tq_enqueue_internal(tq, &tone);
		cw_assert (rv, "failed to enqueue tone #%"PRIu32"/%"PRIu32"", i, tq->capacity);

		/* This tests for correctness of working of the 'enqueue' function. */
		uint32_t len = cw_tq_length_internal(tq);
		cw_assert (len == i + 1, "incorrect tone queue length: %"PRIu32" != %"PRIu32"", len, i + 1);
	}


	/* Try adding a tone to full tq. */
	/* This tests for potential problems with function call.
	   Enqueueing should fail when the queue is full. */
	int rv = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (rv == CW_FAILURE, "was able to add tone to full queue");

	/* This tests for correctness of working of the 'enqueue'
	   function.  Full tq should not grow beyond its capacity. */
	cw_assert (tq->len == tq->capacity, "length of full tone queue is not equal to capacity: %"PRIu32" != %"PRIu32"", tq->len, tq->capacity);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_tq_dequeue_internal()
*/
unsigned int test_cw_tq_dequeue_internal(cw_tone_queue_t *tq)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_dequeue_internal():");

	/* tq should be completely filled after tests of enqueue()
	   function. */

	/* Test some assertions about full tq, just to be sure. */
	cw_assert (tq->capacity == tq->len,
		   "capacity != len of full queue: %"PRIu32" != %"PRIu32"",
		   tq->capacity, tq->len);

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	for (uint32_t i = tq->capacity; i > 0; i--) {

		/* Length of tone queue before dequeue. */
		cw_assert (i == tq->len,
			   "iteration before dequeue doesn't match len: %"PRIu32" != %"PRIu32"",
			   i, tq->len);

		/* This tests for potential problems with function call. */
		int rv = cw_tq_dequeue_internal(tq, &tone);
		cw_assert (rv == CW_TQ_DEQUEUED, "unexpected return value from \"dequeued()\": %d", rv);

		/* Length of tone queue after dequeue. */
		cw_assert (i - 1 == tq->len,
			   "iteration after dequeue doesn't match len: %"PRIu32" != %"PRIu32"",
			   i - 1, tq->len);
	}

	/* Try removing a tone from empty queue. */
	/* This tests for potential problems with function call. */
	int rv = cw_tq_dequeue_internal(tq, &tone);
	cw_assert (rv == CW_TQ_NDEQUEUED_EMPTY, "unexpected return value when dequeueing empty tq: %d", rv);

	/* This tests for correctness of working of the dequeue()
	   function.  Empty tq should stay empty.

	   At this point cw_tq_length_internal() should be tested, so
	   we can use it to verify correctness of dequeue()
	   function. */
	uint32_t len = cw_tq_length_internal(tq);
	cw_assert (len == 0, "non-zero returned length of empty tone queue: len = %"PRIu32"", len);
	cw_assert (tq->len == 0,
		   "length of empty queue is != 0 (%"PRIu32")",
		   tq->len);

	/* Try removing a tone from empty queue. */
	/* This time we should get CW_TQ_NDEQUEUED_IDLE return value. */
	rv = cw_tq_dequeue_internal(tq, &tone);
	cw_assert (rv == CW_TQ_NDEQUEUED_IDLE, "unexpected return value from \"dequeue\" on empty tone queue: %d", rv);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   The second function is just a wrapper for the first one, so this
   test case tests both functions at once.

   tests::cw_tq_is_full_internal()
   tests::cw_is_tone_queue_full()
*/
unsigned int test_cw_tq_is_full_internal(void)
{
	int p = fprintf(stdout, "libcw/tq: cw_tq_is_full_internal():");

	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tq");
	tq->state = CW_TQ_BUSY;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 1, 1, CW_SLOPE_MODE_NO_SLOPES);

	/* Notice the "capacity - 1" in loop condition: we leave one
	   place in tq free so that is_full() called in the loop
	   always returns false. */
	for (uint32_t i = 0; i < tq->capacity - 1; i++) {
		int rv = cw_tq_enqueue_internal(tq, &tone);
		/* The 'enqueue' function has been already tested, but
		   it won't hurt to check this simple assertion here
		   as well. */
		cw_assert (rv == CW_SUCCESS, "failed to enqueue tone #%"PRIu32"", i);

		/* Here is the proper test of tested function. */
		cw_assert (!cw_tq_is_full_internal(tq), "tone queue is full after enqueueing tone #%"PRIu32"", i);
	}

	/* At this point there is still place in tq for one more
	   tone. Enqueue it and verify that the tq is now full. */
	int rv = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (rv == CW_SUCCESS, "failed to enqueue last tone");

	cw_assert (cw_tq_is_full_internal(tq), "tone queue is not full after adding last tone");

	cw_tq_delete_internal(&tq);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   In this function it is done by first enqueueing N known tones to a
   tone queue using cw_tq_enqueue_internal(), and then
   "manually" checking content of tone queue to be sure that all the
   tones are in place.

   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_test_capacity_1(void)
{
	int p = fprintf(stdout, "libcw/tq: testing correctness of handling capacity (1):");

	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	uint32_t capacity = 30;
	uint32_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1". */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t *tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (uint32_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			int rv = cw_tq_enqueue_internal(tq, &tone);
			cw_assert (rv == CW_SUCCESS, "failed to enqueue tone #%"PRIu32"", i);
		}

		/* With the queue filled with valid and known data,
		   it's time to read back the data and verify that the
		   tones were placed in correct positions, as
		   expected. Let's do the readback N times, just for
		   fun. Every time the results should be the same. */

		for (int l = 0; l < 3; l++) {
			for (uint32_t i = 0; i < tq->capacity; i++) {

				uint32_t shifted = (i + head_shifts[s]) % (tq->capacity);
				// fprintf(stderr, "Readback %d: position %"PRIu32": checking tone %"PRIu32", expected %"PRIu32", got %d\n",
				// 	l, shifted, i, i, tq->queue[shifted].frequency);
				cw_assert (tq->queue[shifted].frequency == (int) i, "frequency of dequeued tone is incorrect: %d != %d", tq->queue[shifted].frequency, (int) i);
			}
		}


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);

		s++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Test "capacity" property of tone queue

   Function tests "capacity" property of tone queue, and also tests
   related properties: head and tail.

   In this function it is done by first enqueueing N known tones to a
   tone queue using cw_tq_enqueue_internal(), then dequeueing
   the tones with cw_tq_dequeue_internal() and then checking
   that enqueued tones are the ones that we were expecting to get.

   tests::cw_tq_enqueue_internal()
   tests::cw_tq_dequeue_internal()
*/
unsigned int test_cw_tq_test_capacity_2(void)
{
	int p = fprintf(stdout, "libcw/tq: testing correctness of handling capacity (2):");

	/* We don't need to check tq with capacity ==
	   CW_TONE_QUEUE_CAPACITY_MAX (yet). Let's test a smaller
	   queue. 30 tones will be enough (for now), and 30-4 is a
	   good value for high water mark. */
	uint32_t capacity = 30;
	uint32_t watermark = capacity - 4;

	/* We will do tests of queue with constant capacity, but with
	   different initial position at which we insert first element
	   (tone), i.e. different position of queue's head.

	   Put the guard after "capacity - 1". */
	int head_shifts[] = { 0, 5, 10, 29, -1, 30, -1 };
	int s = 0;

	while (head_shifts[s] != -1) {

		// fprintf(stderr, "\nTesting with head shift = %d\n", head_shifts[s]);

		/* For every new test with new head shift we need a
		   "clean" queue. */
		cw_tone_queue_t *tq = test_cw_tq_capacity_test_init(capacity, watermark, head_shifts[s]);

		/* Fill all positions in queue with tones of known
		   frequency.  If shift_head != 0, the enqueue
		   function should make sure that the enqueued tones
		   are nicely wrapped after end of queue. */
		for (uint32_t i = 0; i < tq->capacity; i++) {
			cw_tone_t tone;
			CW_TONE_INIT(&tone, (int) i, 1000, CW_SLOPE_MODE_NO_SLOPES);

			int rv = cw_tq_enqueue_internal(tq, &tone);
			cw_assert (rv == CW_SUCCESS, "failed to enqueue tone #%"PRIu32"", i);
		}


		/* With the queue filled with valid and known data
		   (tones), it's time to read back the data and verify
		   that the tones were placed in correct positions, as
		   expected.

		   In test_cw_tq_test_capacity_1() we did the
		   readback "manually", this time let's use "dequeue"
		   function to do the job.

		   Since the "dequeue" function moves queue pointers,
		   we can do this test only once (we can't repeat the
		   readback N times with calls to dequeue() expecting
		   the same results). */

		uint32_t i = 0;
		cw_tone_t tone; /* For output only, so no need to initialize. */

		int rv = 0;
		while ((rv = cw_tq_dequeue_internal(tq, &tone))
		       && rv == CW_TQ_DEQUEUED) {

			uint32_t shifted = (i + head_shifts[s]) % (tq->capacity);

			cw_assert (tq->queue[shifted].frequency == (int) i,
				   "position %"PRIu32": checking tone %"PRIu32", expected %"PRIu32", got %d\n",
				   shifted, i, i, tq->queue[shifted].frequency);

			i++;
		}

		cw_assert (i == tq->capacity,
			   "number of dequeues (%"PRIu32") is different than capacity (%"PRIu32")\n",
			   i, tq->capacity);


		/* Matches tone queue creation made in
		   test_cw_tq_capacity_test_init(). */
		cw_tq_delete_internal(&tq);


		s++;
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Create and initialize tone queue for tests of capacity

   Create new tone queue for tests using three given parameters: \p
   capacity, \p high_water_mark, \p head_shift. The function is used
   to create a new tone queue in tests of "capacity" parameter of a
   tone queue.

   First two function parameters are rather boring. What is
   interesting is the third parameter: \p head_shift.

   In general the behaviour of tone queue (a circular list) should be
   independent of initial position of queue's head (i.e. from which
   position in the queue we start adding new elements to the queue).

   By initializing the queue with different initial positions of head
   pointer, we can test this assertion about irrelevance of initial
   head position.

   Returned pointer is owned by caller.

   tests::cw_tq_set_capacity_internal()

   \param capacity - intended capacity of tone queue
   \param high_water_mark - high water mark to be set for tone queue
   \param head_shift - position of first element that will be inserted in empty queue

   \return newly allocated and initialized tone queue
*/
cw_tone_queue_t *test_cw_tq_capacity_test_init(uint32_t capacity, uint32_t high_water_mark, int head_shift)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create new tone queue");
	tq->state = CW_TQ_BUSY;

	int rv = cw_tq_set_capacity_internal(tq, capacity, high_water_mark);
	cw_assert (rv == CW_SUCCESS, "failed to set capacity/high water mark");
	cw_assert (tq->capacity == capacity, "incorrect capacity: %"PRIu32" != %"PRIu32"", tq->capacity, capacity);
	cw_assert (tq->high_water_mark == high_water_mark, "incorrect high water mark: %"PRIu32" != %"PRIu32"", tq->high_water_mark, high_water_mark);

	/* Initialize *all* tones with known value. Do this manually,
	   to be 100% sure that all tones in queue table have been
	   initialized. */
	for (int i = 0; i < CW_TONE_QUEUE_CAPACITY_MAX; i++) {
		CW_TONE_INIT(&(tq->queue[i]), 10000 + i, 1, CW_SLOPE_MODE_STANDARD_SLOPES);
	}

	/* Move head and tail of empty queue to initial position. The
	   queue is empty - the initialization of fields done above is not
	   considered as real enqueueing of valid tones. */
	tq->tail = head_shift;
	tq->head = tq->tail;
	tq->len = 0;

	/* TODO: why do this here? */
	tq->state = CW_TQ_BUSY;

	return tq;
}





/**
   \brief Test the limits of the parameters to the tone queue routine

   tests::cw_tq_enqueue_internal()
*/
unsigned int test_cw_tq_enqueue_internal_2(void)
{
	cw_tone_queue_t *tq = cw_tq_new_internal();
	cw_assert (tq, "failed to create a tone queue\n");
	cw_tone_t tone;


	int f_min, f_max;
	cw_get_frequency_limits(&f_min, &f_max);


	/* Test 1: invalid length of tone. */
	errno = 0;
	tone.len = -1;            /* Invalid length. */
	tone.frequency = f_min;   /* Valid frequency. */
	int status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with invalid length.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 2: tone's frequency too low. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_min - 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too low frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));


	/* Test 3: tone's frequency too high. */
	errno = 0;
	tone.len = 100;                /* Valid length. */
	tone.frequency = f_max + 1;    /* Invalid frequency. */
	status = cw_tq_enqueue_internal(tq, &tone);
	cw_assert (status == CW_FAILURE, "enqueued tone with too high frequency.\n");
	cw_assert (errno == EINVAL, "bad errno: %s\n", strerror(errno));

	cw_tq_delete_internal(&tq);
	cw_assert (!tq, "tone queue not deleted properly\n");

	int n = printf("libcw/tq: cw_tq_enqueue_internal():");
	CW_TEST_PRINT_TEST_RESULT (false, n);

	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
