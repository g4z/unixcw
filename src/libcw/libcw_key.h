/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include <stdbool.h>





#include "libcw_gen.h"
#include "libcw_rec.h"





/* KS stands for Keyer State. */
enum {
	KS_IDLE,
	KS_IN_DOT_A,
	KS_IN_DASH_A,
	KS_AFTER_DOT_A,
	KS_AFTER_DASH_A,
	KS_IN_DOT_B,
	KS_IN_DASH_B,
	KS_AFTER_DOT_B,
	KS_AFTER_DASH_B
};





struct cw_key_struct {
	/* Straight key and iambic keyer needs a generator to produce
	   a sound on "Key Down" events. Maybe we don't always need a
	   sound, but sometimes we do want to have it.

	   Additionally iambic keyer needs a generator for timing
	   purposes.

	   In any case - a key needs to have access to a generator
	   (but a generator doesn't need a key). This is why the key
	   data type has a "generator" field, not the other way
	   around. */
	cw_gen_t *gen;


	/* There should be a binding between key and a receiver.

	   The receiver can get it's properly formed input data (key
	   down/key up events) from any source, so it's independent
	   from key. On the other hand the key without receiver is
	   rather useless. Therefore I think that the key should
	   contain reference to a receiver, not the other way
	   around. */
	cw_rec_t *rec;


	/* External "on key state change" callback function and its
	   argument.

	   It may be useful for a client to have this library control
	   an external keying device, for example, an oscillator, or a
	   transmitter.  Here is where we keep the address of a
	   function that is passed to us for this purpose, and a void*
	   argument for it. */
	void (*key_callback)(void*, int);
	void *key_callback_arg;


	/* Straight key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} sk;


	/* Iambic keyer.  The keyer functions maintain the current
	   known state of the paddles, and latch false-to-true
	   transitions while busy, to form the iambic effect.  For
	   Curtis mode B, the keyer also latches any point where both
	   paddle states are true at the same time. */
	struct {
		int graph_state;       /* State of iambic keyer state machine. */
		int key_value;         /* Open/Closed, Space/Mark, NoSound/Sound. */

		bool dot_paddle;       /* Dot paddle state */
		bool dash_paddle;      /* Dash paddle state */

		bool dot_latch;        /* Dot false->true latch */
		bool dash_latch;       /* Dash false->true latch */

		/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
		   differ slightly, and some people have a preference for one or the other.
		   Mode A is a bit less timing-critical, so we'll make that the default. */
		bool curtis_mode_b;

		bool curtis_b_latch;   /* Curtis Dot&Dash latch */

		bool lock;             /* FIXME: describe why we need this flag. */

		struct timeval *timer; /* Timer for receiving of iambic keying, owned by client code. */

		/* Generator associated with the keyer. Should never
		   be NULL as iambic keyer *needs* a generator to
		   function properly (and to generate audible tones).
		   Set using
		   cw_key_register_generator_internal(). */
		/* No separate generator, use cw_key_t->gen. */
	} ik;


	/* Tone-queue key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} tk;
};





typedef struct cw_key_struct cw_key_t;





void cw_key_tk_set_value_internal(volatile cw_key_t *key, int key_state);

void cw_key_register_generator_internal(volatile cw_key_t *key, cw_gen_t *gen);
void cw_key_register_receiver_internal(volatile cw_key_t *key, cw_rec_t *rec);

int  cw_key_ik_update_graph_state_internal(volatile cw_key_t *keyer);
void cw_key_ik_increment_timer_internal(volatile cw_key_t *keyer, int usecs);
void cw_key_register_keying_callback_internal(volatile cw_key_t *key, void (*callback_func)(void*, int), void *callback_arg);
void cw_key_ik_register_timer_internal(volatile cw_key_t *key, struct timeval *timer);
void cw_key_ik_enable_curtis_mode_b_internal(volatile cw_key_t *key);
void cw_key_ik_disable_curtis_mode_b_internal(volatile cw_key_t *key);
bool cw_key_ik_get_curtis_mode_b_state_internal(volatile cw_key_t *key);
int  cw_key_ik_notify_paddle_event_internal(volatile cw_key_t *key, int dot_paddle_state, int dash_paddle_state);
int  cw_key_ik_notify_dash_paddle_event_internal(volatile cw_key_t *key, int dash_paddle_state);
int  cw_key_ik_notify_dot_paddle_event_internal(volatile cw_key_t *key, int dot_paddle_state);
void cw_key_ik_get_paddles_internal(volatile cw_key_t *key, int *dot_paddle_state, int *dash_paddle_state);
void cw_key_ik_get_paddle_latches_internal(volatile cw_key_t *key, int *dot_paddle_latch_state, int *dash_paddle_latch_state);
bool cw_key_ik_is_busy_internal(volatile cw_key_t *key);
int  cw_key_ik_wait_for_element_internal(volatile cw_key_t *key);
int  cw_key_ik_wait_for_keyer_internal(volatile cw_key_t *key);
void cw_key_ik_reset_internal(volatile cw_key_t *key);

int  cw_key_sk_notify_event_internal(volatile cw_key_t *key, int key_state);
int  cw_key_sk_get_state_internal(volatile cw_key_t *key);
bool cw_key_sk_is_busy_internal(volatile cw_key_t *key);
void cw_key_sk_reset_internal(volatile cw_key_t *key);





#endif // #ifndef H_LIBCW_KEY
