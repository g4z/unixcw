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
   \file libcw.c

*/



#include <errno.h> /* EINVAL on FreeBSD */

#include "libcw.h"
#include "libcw_gen.h"
#include "libcw_rec.h"
#include "libcw_key.h"
#include "libcw_debug.h"





/* Main container for data related to generating audible Morse code.
   This is a global variable in library file, but in future the
   variable will be moved from the file to client code.

   This is a global variable that should be converted into a function
   argument; this pointer should exist only in client's code, should
   initially be returned by new(), and deleted by delete().

   TODO: perform the conversion later, when you figure out ins and
   outs of the library. */
cw_gen_t *cw_generator = NULL;





/* From libcw_debug.c. */
extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





static cw_rec_t cw_receiver = {

	.state = RS_IDLE,


	.speed                      = CW_SPEED_INITIAL,
	.tolerance                  = CW_TOLERANCE_INITIAL,
	.gap                        = CW_GAP_INITIAL,
	.is_adaptive_receive_mode   = CW_REC_ADAPTIVE_MODE_INITIAL,
	.noise_spike_threshold      = CW_REC_NOISE_THRESHOLD_INITIAL,


	/* TODO: this variable is not set in
	   cw_rec_reset_receive_parameters_internal(). Why is it
	   separated from the four main variables? Is it because it is
	   a derivative of speed? But speed is a derivative of this
	   variable in adaptive speed mode. */
	.adaptive_speed_threshold = CW_REC_SPEED_THRESHOLD_INITIAL,


	.mark_start = { 0, 0 },
	.mark_end   = { 0, 0 },


	.representation[0] = '\0',
	.representation_ind = 0,


	.dot_len_ideal = 0,
	.dot_len_min = 0,
	.dot_len_max = 0,

	.dash_len_ideal = 0,
	.dash_len_min = 0,
	.dash_len_max = 0,

	.eom_len_ideal = 0,
	.eom_len_min = 0,
	.eom_len_max = 0,

	.eoc_len_ideal = 0,
	.eoc_len_min = 0,
	.eoc_len_max = 0,

	.additional_delay = 0,
	.adjustment_delay = 0,


	.parameters_in_sync = false,


	.statistics = { {0, 0} },
	.statistics_ind = 0,


	.dot_averaging  = { {0}, 0, 0, 0 },
	.dash_averaging = { {0}, 0, 0, 0 },
};





static volatile cw_key_t cw_key = {
	.gen = NULL,


	.rec = &cw_receiver,


	.key_callback = NULL,
	.key_callback_arg = NULL,


	.sk = {
		.key_value = CW_KEY_STATE_OPEN
	},


	.ik = {
		.graph_state = KS_IDLE,
		.key_value = CW_KEY_STATE_OPEN,

		.dot_paddle = false,
		.dash_paddle = false,

		.dot_latch = false,
		.dash_latch = false,

		.curtis_mode_b = false,
		.curtis_b_latch = false,

		.lock = false,

		.timer = NULL
	},


	.tk = {
		.key_value = CW_KEY_STATE_OPEN
	}
};





/* ******************************************************************** */
/*                              Generator                               */
/* ******************************************************************** */





/**
   \brief Create new generator

   Allocate memory for new generator data structure, set up default values
   of some of the generator's properties.
   The function does not start the generator (generator does not produce
   a sound), you have to use cw_generator_start() for this.

   Notice that the function doesn't return a generator variable. There
   is at most one generator variable at any given time. You can't have
   two generators. In some future version of the library the function
   will return pointer to newly allocated generator, and then you
   could have as many of them as you want, but not yet.

   \p audio_system can be one of following: NULL, console, OSS, ALSA,
   PulseAudio, soundcard. See "enum cw_audio_systems" in libcw.h for
   exact names of symbolic constants.

   \param audio_system - audio system to be used by the generator
   \param device - name of audio device to be used; if NULL then library will use default device.
*/
int cw_generator_new(int audio_system, const char *device)
{
	cw_generator = cw_gen_new_internal(audio_system, device);
	if (!cw_generator) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: can't create generator");
		return CW_FAILURE;
	} else {
		/* For some (all?) applications a key needs to have
		   some generator associated with it. */
		cw_key_register_generator_internal(&cw_key, cw_generator);

		return CW_SUCCESS;
	}
}





/**
   \brief Deallocate generator

   Deallocate/destroy generator data structure created with call
   to cw_generator_new(). You can't start nor use the generator
   after the call to this function.
*/
void cw_generator_delete(void)
{
	cw_gen_delete_internal(&cw_generator);

	return;
}





/**
   \brief Start a generator

   Start producing tones using generator created with
   cw_generator_new(). The source of tones is a tone queue associated
   with the generator. If the tone queue is empty, the generator will
   wait for new tones to be queued.

   \return CW_FAILURE on errors
   \return CW_SUCCESS on success
*/
int cw_generator_start(void)
{
	return cw_gen_start_internal(cw_generator);
}





/**
   \brief Shut down a generator

   Silence tone generated by generator (level of generated sine wave is
   set to zero, with falling slope), and shut the generator down.

   The shutdown does not erase generator's configuration.

   If you want to have this generator running again, you have to call
   cw_generator_start().
*/
void cw_generator_stop(void)
{
	cw_gen_stop_internal(cw_generator);

	return;
}





/**
   \brief Delete a generator - wrapper used in libcw_utils.c
*/
void cw_generator_delete_internal(void)
{
	if (cw_generator) {
		cw_gen_delete_internal(&cw_generator);
	}

	return;
}









/**
   \brief Set sending speed of generator

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of send speed to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_send_speed(int new_value)
{
	int rv = cw_gen_set_speed_internal(cw_generator, new_value);
	return rv;
}





/**
   \brief Set frequency of generator

   Set frequency of sound wave generated by generator.
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   See libcw.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   errno is set to EINVAL if \p new_value is out of range.

   \param new_value - new value of frequency to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_frequency(int new_value)
{
	int rv = cw_gen_set_frequency_internal(cw_generator, new_value);
	return rv;
}





/**
   \brief Set volume of generator

   Set volume of sound wave generated by generator.
   The volume must be within limits marked by CW_VOLUME_MIN and CW_VOLUME_MAX.

   Note that volume settings are not fully possible for the console speaker.
   In this case, volume settings greater than zero indicate console speaker
   sound is on, and setting volume to zero will turn off console speaker
   sound.

   See libcw.h/CW_VOLUME_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of volume.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_volume_functions()
   testedin::test_parameter_ranges()

   \param new_value - new value of volume to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_volume(int new_value)
{
	int rv = cw_gen_set_volume_internal(cw_generator, new_value);
	return rv;
}





/**
   \brief Set sending gap of generator

   See libcw.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.
   errno is set to EINVAL if \p new_value is out of range.

   Notice that this function also sets the same gap value for
   library's receiver.

   testedin::test_parameter_ranges()

   \param new_value - new value of gap to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_gap(int new_value)
{
	int rv = cw_gen_set_gap_internal(cw_generator, new_value);
	if (rv != CW_FAILURE) {
		/* Ideally generator and receiver should have their
		   own, separate cw_set_gap() functions. Unfortunately
		   this is not the case (for now) so gap should be set
		   here for receiver as well.

		   TODO: add cw_set_gap() function for receiver. */
		rv = cw_rec_set_gap_internal(&cw_receiver, new_value);
	}
	return rv;
}





/**
   \brief Set sending weighting for generator

   See libcw.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of weighting to be assigned for generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_weighting(int new_value)
{
	int rv = cw_gen_set_weighting_internal(cw_generator, new_value);
	return rv;
}





/**
   \brief Get sending speed from generator

   testedin::test_parameter_ranges()

   \return current value of the generator's send speed
*/
int cw_get_send_speed(void)
{
	return cw_gen_get_speed_internal(cw_generator);
}





/**
   \brief Get frequency from generator

   Function returns "frequency" parameter of generator,
   even if the generator is stopped, or volume of generated sound is zero.

   testedin::test_parameter_ranges()

   \return current value of generator's frequency
*/
int cw_get_frequency(void)
{
	return cw_gen_get_frequency_internal(cw_generator);
}





/**
   \brief Get sound volume from generator

   Function returns "volume" parameter of generator,
   even if the generator is stopped.

   testedin::test_volume_functions()
   testedin::test_parameter_ranges()

   \return current value of generator's sound volume
*/
int cw_get_volume(void)
{
	return cw_gen_get_volume_internal(cw_generator);
}





/**
   \brief Get sending gap from generator

   testedin::test_parameter_ranges()

   \return current value of generator's sending gap
*/
int cw_get_gap(void)
{
	return cw_gen_get_gap_internal(cw_generator);
}





/**
   \brief Get sending weighting from generator

   testedin::test_parameter_ranges()

   \return current value of generator's sending weighting
*/
int cw_get_weighting(void)
{
	return cw_gen_get_weighting_internal(cw_generator);
}





/**
   \brief Get timing parameters for sending

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param end_of_element_usecs
   \param end_of_character_usecs
   \param end_of_word_usecs
   \param additional_usecs
   \param adjustment_usecs
*/
void cw_get_send_parameters(int *dot_usecs, int *dash_usecs,
			    int *end_of_element_usecs,
			    int *end_of_character_usecs, int *end_of_word_usecs,
			    int *additional_usecs, int *adjustment_usecs)
{
	cw_gen_get_send_parameters_internal(cw_generator,
					    dot_usecs, dash_usecs,
					    end_of_element_usecs,
					    end_of_character_usecs, end_of_word_usecs,
					    additional_usecs, adjustment_usecs);

	return;
}





/**
   \brief Low-level primitive for sending a dot mark

   Low-level primitive function able to play/send single dot mark. The
   function appends to a tone queue a normal inter-mark gap after the
   dot mark.

   testedin::test_send_primitives()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_dot(void)
{
	return cw_gen_play_mark_internal(cw_generator, CW_DOT_REPRESENTATION);
}





/**
   \brief Low-level primitive for sending a dash mark

   Low-level primitive function able to play/send single dash mark.
   The function appends to a tone queue a normal inter-mark gap after
   the dash mark.

   testedin::test_send_primitives()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_dash(void)
{
	return cw_gen_play_mark_internal(cw_generator, CW_DASH_REPRESENTATION);
}





/**

   The function plays space timed to exclude the expected prior
   dot/dash inter-mark gap.
   FIXME: fix this description.

   testedin::test_send_primitives()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character_space(void)
{
	return cw_gen_play_eoc_space_internal(cw_generator);
}





/**

   The function sends space timed to exclude both the expected prior
   dot/dash inter-mark gap and the prior end of character space.
   FIXME: fix this description.

   testedin::test_send_primitives()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_word_space(void)
{
	return cw_gen_play_eow_space_internal(cw_generator);
}





/**
   \brief Check, then send the given string as dots and dashes.

   The representation passed in is assumed to be a complete Morse
   character; that is, all post-character delays will be added when
   the character is sent.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone
   queue is full, or if there is insufficient space to queue the tones
   or the representation.

   testedin::test_representations()

   \param representation - representation to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_representation(const char *representation)
{
	return cw_gen_play_representation_internal(cw_generator, representation, false);
}





/**
   \brief Check, then send the given string as dots and dashes

   The \p representation passed in is assumed to be only part of a larger
   Morse representation; that is, no post-character delays will be added
   when the character is sent.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EINVAL if any
   character of the representation is invalid, EBUSY if the sound card,
   console speaker, or keying system is busy, or EAGAIN if the tone queue
   is full, or if there is insufficient space to queue the tones for
   the representation.

   testedin::test_representations()
*/
int cw_send_representation_partial(const char *representation)
{
	return cw_gen_play_representation_internal(cw_generator, representation, true);
}





/**
   \brief Look up and send a given ASCII character as Morse

   The end of character delay is appended to the Morse sent.

   On success the routine returns CW_SUCCESS.
   On failure the function returns CW_FAILURE and sets errno.

   errno is set to ENOENT if the given character \p c is not a valid
   Morse character.
   errno is set to EBUSY if current audio sink or keying system is
   busy.
   errno is set to EAGAIN if the generator's tone queue is full, or if
   there is insufficient space to queue the tones for the character.

   This routine returns as soon as the character has been successfully
   queued for sending; that is, almost immediately.  The actual sending
   happens in background processing.  See cw_wait_for_tone() and
   cw_wait_for_tone_queue() for ways to check the progress of sending.

   testedin::test_send_character_and_string()

   \param c - character to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character(char c)
{
	return cw_gen_play_character_internal(cw_generator, c);
}





/**
   \brief Look up and send a given ASCII character as Morse code

   "partial" means that the "end of character" delay is not appended
   to the Morse code sent by the function, to support the formation of
   combination characters.

   On success the function returns CW_SUCCESS.
   On failure the function returns CW_FAILURE and sets errno.

   errno is set to ENOENT if the given character \p c is not a valid
   Morse character.
   errno is set to EBUSY if the audio sink or keying system is busy.
   errno is set to EAGAIN if the tone queue is full, or if there is
   insufficient space to queue the tones for the character.

   This routine queues its arguments for background processing.  See
   cw_wait_for_tone() and cw_wait_for_tone_queue() for ways to check
   the progress of sending.

   \param c - character to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_character_partial(char c)
{
	return cw_gen_play_character_parital_internal(cw_generator, c);
}





/**
   \brief Send a given ASCII string in Morse code

   errno is set to ENOENT if any character in the string is not a
   valid Morse character.

   errno is set to EBUSY if audio sink or keying system is busy.

   errno is set to EAGAIN if the tone queue is full or if the tone
   queue runs out of space part way through queueing the string.
   However, an indeterminate number of the characters from the string
   will have already been queued.

   For safety, clients can ensure the tone queue is empty before
   queueing a string, or use cw_send_character() if they need finer
   control.

   This routine queues its arguments for background processing, the
   actual sending happens in background processing. See
   cw_wait_for_tone() and cw_wait_for_tone_queue() for ways to check
   the progress of sending.

   testedin::test_send_character_and_string()

   \param string - string to send

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_send_string(const char *string)
{
	return cw_gen_play_string_internal(cw_generator, string);
}





/**
   \brief Reset send/receive parameters

   Reset the library speed, frequency, volume, gap, tolerance, weighting,
   adaptive receive, and noise spike threshold to their initial default
   values: send/receive speed 12 WPM, volume 70 %, frequency 800 Hz,
   gap 0 dots, tolerance 50 %, and weighting 50 %.
*/
void cw_reset_send_receive_parameters(void)
{
	cw_gen_reset_send_parameters_internal(cw_generator);
	cw_rec_reset_receive_parameters_internal(&cw_receiver);

	/* Reset requires resynchronization. */
	cw_gen_sync_parameters_internal(cw_generator);
	cw_rec_sync_parameters_internal(&cw_receiver);

	return;
}





/**
   \brief Return char string with console device path

   Returned pointer is owned by library.

   \return char string with current console device path
*/
const char *cw_get_console_device(void)
{
	return cw_generator->audio_device;
}





/**
   \brief Return char string with soundcard device name/path

   Returned pointer is owned by library.

   \return char string with current soundcard device name or device path
*/
const char *cw_get_soundcard_device(void)
{
	return cw_generator->audio_device;
}





/**
   \brief Get a readable label of current audio system

   The function returns one of following strings:
   None, Null, Console, OSS, ALSA, PulseAudio, Soundcard

   \return audio system's label
*/
const char *cw_generator_get_audio_system_label(void)
{
	return cw_get_audio_system_label(cw_generator->audio_system);
}





/* ******************************************************************** */
/*                             Tone queue                               */
/* ******************************************************************** */





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

   testedin::test_tone_queue_callback()

   \param callback_func - callback function to be registered
   \param callback_arg - argument for callback_func to pass return value
   \param level - low level of queue triggering callback call

   \return CW_SUCCESS on successful registration
   \return CW_FAILURE on failure
*/
int cw_register_tone_queue_low_callback(void (*callback_func)(void*), void *callback_arg, int level)
{
	return cw_tq_register_low_level_callback_internal(cw_generator->tq, callback_func, callback_arg, level);
}









/**
   \brief Check if tone sender is busy

   Indicate if the tone sender is busy.

   \return true if there are still entries in the tone queue
   \return false if the queue is empty
*/
bool cw_is_tone_busy(void)
{
	return cw_tq_is_busy_internal(cw_generator->tq);
}





/**
   \brief Wait for the current tone to complete

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns CW_FAILURE, with errno set to EDEADLK,
   to avoid indefinite waits.

   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone(void)
{
	return cw_tq_wait_for_tone_internal(cw_generator->tq);
}





/**
   \brief Wait for the tone queue to drain

   The routine returns CW_SUCCESS on success. If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK,
   to avoid indefinite waits.

   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()
   testedin::test_tone_queue_3()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue(void)
{
	return cw_tq_wait_for_tone_queue_internal(cw_generator->tq);
}






/**
   \brief Wait for the tone queue to drain until only as many tones as given in level remain queued

   This routine is for use by programs that want to optimize themselves
   to avoid the cleanup that happens when the tone queue drains completely;
   such programs have a short time in which to add more tones to the queue.

   The routine returns CW_SUCCESS on success.  If called with SIGALRM
   blocked, the routine returns false, with errno set to EDEADLK, to
   avoid indefinite waits.

   \param level - low level in queue, at which to return

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_tone_queue_critical(int level)
{
	return cw_tq_wait_for_level_internal(cw_generator->tq, (uint32_t) level);
}





/**
   \brief Indicate if the tone queue is full

   testedin::test_cw_tq_is_full_internal()

   \return true if tone queue is full
   \return false if tone queue is not full
*/
bool cw_is_tone_queue_full(void)
{
	return cw_tq_is_full_internal(cw_generator->tq);
}





/**
   \brief Return the number of entries the tone queue can accommodate

   testedin::test_tone_queue_3()
   testedin::test_cw_tq_get_capacity_internal()
*/
int cw_get_tone_queue_capacity(void)
{
	return (int) cw_tq_get_capacity_internal(cw_generator->tq);
}





/**
   \brief Return the number of entries currently pending in the tone queue

   testedin::test_cw_tq_length_internal()
   testedin::test_tone_queue_1()
   testedin::test_tone_queue_3()
*/
int cw_get_tone_queue_length(void)
{
	return (int) cw_tq_length_internal(cw_generator->tq);
}





/**
   \brief Cancel all pending queued tones, and return to silence.

   If there is a tone in progress, the function will wait until this
   last one has completed, then silence the tones.

   This function may be called with SIGALRM blocked, in which case it
   will empty the queue as best it can, then return without waiting for
   the final tone to complete.  In this case, it may not be possible to
   guarantee silence after the call.
*/
void cw_flush_tone_queue(void)
{
	/* This function locks and unlocks mutex. */
	cw_tq_flush_internal(cw_generator->tq);

	/* Force silence on the speaker anyway, and stop any background
	   soundcard tone generation. */
	cw_gen_silence_internal(cw_generator);
	//cw_finalization_schedule_internal();

	return;
}





/**
   Cancel all pending queued tones, reset any queue low callback registered,
   and return to silence.  This function is suitable for calling from an
   application exit handler.
*/
void cw_reset_tone_queue(void)
{
	cw_tq_reset_internal(cw_generator->tq);

	/* Silence sound and stop any background soundcard tone generation. */
	cw_gen_silence_internal(cw_generator);
	//cw_finalization_schedule_internal();

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_TONE_QUEUE, CW_DEBUG_INFO,
		      "libcw: tone queue: reset");

	return;
}





/**
   \brief Primitive access to simple tone generation

   This routine queues a tone of given duration and frequency.
   The routine returns CW_SUCCESS on success.  If usec or frequency
   are invalid, it returns CW_FAILURE with errno set to EINVAL.
   If the sound card, console speaker, or keying function are busy,
   it returns CW_FAILURE  with errno set to EBUSY.  If the tone queue
   is full, it returns false with errno set to EAGAIN.

   testedin::test_tone_queue_0()
   testedin::test_tone_queue_1()
   testedin::test_tone_queue_2()
   testedin::test_tone_queue_3()

   \param usecs - duration of queued tone, in microseconds
   \param frequency - frequency of queued tone

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_queue_tone(int usecs, int frequency)
{
	/* Check the arguments given for realistic values.  This test
	   is left here for legacy reasons. Don't change it. */
	if (usecs < 0
	    || frequency < CW_FREQUENCY_MIN
	    || frequency > CW_FREQUENCY_MAX) {

		errno = EINVAL;
		return CW_FAILURE;
	}

	cw_tone_t tone;
	CW_TONE_INIT(&tone, frequency, usecs, CW_SLOPE_MODE_STANDARD_SLOPES);
	int rv = cw_tq_enqueue_internal(cw_generator->tq, &tone);

	return rv;
}





/* ******************************************************************** */
/*                              Receiver                                */
/* ******************************************************************** */





/**
   \brief Set receiving speed of receiver

   See documentation of cw_set_send_speed() for more information.

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of receive speed.
   errno is set to EINVAL if \p new_value is out of range.
   errno is set to EPERM if adaptive receive speed tracking is enabled.

   testedin::test_parameter_ranges()

   \param new_value - new value of receive speed to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_receive_speed(int new_value)
{
	return cw_rec_set_speed_internal(&cw_receiver, new_value);
}





/**
   \brief Get receiving speed from receiver

   testedin::test_parameter_ranges()

   \return current value of the receiver's receive speed
*/
int cw_get_receive_speed(void)
{
	return (int) cw_rec_get_speed_internal(&cw_receiver);
}





/**
   \brief Set tolerance for receiver

   See libcw.h/CW_TOLERANCE_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of tolerance.
   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param new_value - new value of tolerance to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_tolerance(int new_value)
{
	return cw_rec_set_tolerance_internal(&cw_receiver, new_value);
}





/**
   \brief Get tolerance from receiver

   testedin::test_parameter_ranges()

   \return current value of receiver's tolerance
*/
int cw_get_tolerance(void)
{
	return cw_rec_get_tolerance_internal(&cw_receiver);
}





/**
   \brief Get timing parameters for receiving, and adaptive threshold

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param dot_usecs
   \param dash_usecs
   \param dot_min_usecs
   \param dot_max_usecs
   \param dash_min_usecs
   \param dash_max_usecs
   \param end_of_element_min_usecs
   \param end_of_element_max_usecs
   \param end_of_element_ideal_usecs
   \param end_of_character_min_usecs
   \param end_of_character_max_usecs
   \param end_of_character_ideal_usecs
   \param adaptive_threshold
*/
void cw_get_receive_parameters(int *dot_usecs, int *dash_usecs,
			       int *dot_min_usecs, int *dot_max_usecs,
			       int *dash_min_usecs, int *dash_max_usecs,
			       int *end_of_element_min_usecs,
			       int *end_of_element_max_usecs,
			       int *end_of_element_ideal_usecs,
			       int *end_of_character_min_usecs,
			       int *end_of_character_max_usecs,
			       int *end_of_character_ideal_usecs,
			       int *adaptive_threshold)
{
	cw_rec_get_parameters_internal(&cw_receiver,
				       dot_usecs, dash_usecs,
				       dot_min_usecs, dot_max_usecs,
				       dash_min_usecs, dash_max_usecs,
				       end_of_element_min_usecs,
				       end_of_element_max_usecs,
				       end_of_element_ideal_usecs,
				       end_of_character_min_usecs,
				       end_of_character_max_usecs,
				       end_of_character_ideal_usecs,
				       adaptive_threshold);

	return;
}





/**
   \brief Set noise spike threshold for receiver

   Set the period shorter than which, on receive, received marks are ignored.
   This allows the "receive mark" functions to apply noise canceling for very
   short apparent marks.
   For useful results the value should never exceed the dot length of a dot at
   maximum speed: 20000 microseconds (the dot length at 60WPM).
   Setting a noise threshold of zero turns off receive mark noise canceling.

   The default noise spike threshold is 10000 microseconds.

   errno is set to EINVAL if \p new_value is out of range.

   \param new_value - new value of noise spike threshold to be assigned to receiver

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_set_noise_spike_threshold(int new_value)
{
	return cw_rec_set_noise_spike_threshold_internal(&cw_receiver, new_value);
}





/**
   \brief Get noise spike threshold from receiver

   See documentation of cw_set_noise_spike_threshold() for more information

   \return current value of receiver's threshold
*/
int cw_get_noise_spike_threshold(void)
{
	return cw_rec_get_noise_spike_threshold_internal(&cw_receiver);
}





/**
   \brief Calculate and return receiver's timing statistics

   These statistics may be used to obtain a measure of the accuracy of
   received CW.  The values \p dot_sd and \p dash_sd contain the standard
   deviation of dot and dash lengths from the ideal values, and
   \p element_end_sd and \p character_end_sd the deviations for inter
   element and inter character spacing.  Statistics are held for all
   timings in a 256 element circular buffer.  If any statistic cannot
   be calculated, because no records for it exist, the returned value
   is 0.0.  Use NULL for the pointer argument to any statistic not required.

   \param dot_sd
   \param dash_sd
   \param element_end_sd
   \param character_end_sd
*/
void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
			       double *element_end_sd, double *character_end_sd)
{
	cw_rec_get_statistics_internal(&cw_receiver, dot_sd, dash_sd,
				       element_end_sd, character_end_sd);

	return;
}





/**
   \brief Clear the receive statistics buffer

   Clear the receive statistics buffer by removing all records from it and
   returning it to its initial default state.
*/
void cw_reset_receive_statistics(void)
{
	cw_rec_reset_receive_statistics_internal(&cw_receiver);

	return;
}





/**
   \brief Enable adaptive receive speed tracking

   If adaptive speed tracking is enabled, the receive functions will
   attempt to automatically adjust the receive speed setting to match
   the speed of the incoming Morse code. If it is disabled, the receive
   functions will use fixed speed settings, and reject incoming Morse
   which is not at the expected speed.

   Adaptive speed tracking uses a moving average length of the past N marks
   as its baseline for tracking speeds.  The default state is adaptive speed
   tracking disabled.
*/
void cw_enable_adaptive_receive(void)
{
	cw_rec_set_adaptive_mode_internal(&cw_receiver, true);
	return;
}





/**
   \brief Disable adaptive receive speed tracking

   See documentation of cw_enable_adaptive_receive() for more information
*/
void cw_disable_adaptive_receive(void)
{
	cw_rec_set_adaptive_mode_internal(&cw_receiver, false);
	return;
}





/**
   \brief Get adaptive receive speed tracking flag

   The function returns state of "adaptive receive enabled" flag.
   See documentation of cw_enable_adaptive_receive() for more information

   \return true if adaptive speed tracking is enabled
   \return false otherwise
*/
bool cw_get_adaptive_receive_state(void)
{
	return cw_rec_get_adaptive_mode_internal(&cw_receiver);
}





/**
   \brief Signal beginning of receive mark

   Called on the start of a receive mark.  If the \p timestamp is NULL, the
   current timestamp is used as beginning of mark.

   The function should be called by client application when pressing a
   key down (closing a circuit) has been detected by client
   application.

   On error the function returns CW_FAILURE, with errno set to ERANGE if
   the call is directly after another cw_start_receive_tone() call or if
   an existing received character has not been cleared from the buffer,
   or EINVAL if the timestamp passed in is invalid.

   \param timestamp - time stamp of "key down" event

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise (with errno set)
*/
int cw_start_receive_tone(const struct timeval *timestamp)
{
	return cw_rec_mark_begin_internal(&cw_receiver, timestamp);
}





/**
   \brief Signal end of mark

   The function should be called by client application when releasing
   a key (opening a circuit) has been detected by client application.

   If the \p timestamp is NULL, the current time is used as timestamp
   of end of mark.

   On success, the routine adds a dot or dash to the receiver's
   representation buffer, and returns CW_SUCCESS.

   On failure, it returns CW_FAIURE, with errno set to:
   ERANGE if the call was not preceded by a cw_start_receive_tone() call,
   EINVAL if the timestamp passed in is not valid,
   ENOENT if the mark length was out of bounds for the permissible
   dot and dash lengths and fixed speed receiving is selected,
   ENOMEM if the receiver's representation buffer is full,
   EAGAIN if the mark was shorter than the threshold for noise and was
   therefore ignored.

   \param timestamp - time stamp of "key up" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_end_receive_tone(const struct timeval *timestamp)
{
	return cw_rec_mark_end_internal(&cw_receiver, timestamp);
}





/**
   \brief Add a dot to the receiver's representation buffer

   Documentation for both cw_receive_buffer_dot() and cw_receive_buffer_dash():

   Since we can't add a mark to the buffer without any
   accompanying timing information, the functions accepts \p timestamp
   of the "end of mark" event.  If the \p timestamp is NULL, the
   current timestamp is used.

   These routines are for client code that has already determined
   whether a dot or dash was received by a method other than calling
   the routines cw_start_receive_tone() and cw_end_receive_tone().

   On success, the relevant mark is added to the receiver's
   representation buffer.

   On failure, the routines return CW_FAILURE, with errno set to
   ERANGE if preceded by a cw_start_receive_tone() call with no matching
   cw_end_receive_tone() or if an error condition currently exists
   within the receiver's buffer, or ENOMEM if the receiver's
   representation buffer is full.

   \param timestamp - timestamp of "end of dot" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dot(const struct timeval *timestamp)
{
	return cw_rec_add_mark_internal(&cw_receiver, timestamp, CW_DOT_REPRESENTATION);
}





/**
   \brief Add a dash to the receiver's representation buffer

   See documentation of cw_receive_buffer_dot() for more information.

   \param timestamp - timestamp of "end of dash" event

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_buffer_dash(const struct timeval *timestamp)
{
	return cw_rec_add_mark_internal(&cw_receiver, timestamp, CW_DASH_REPRESENTATION);
}





/**
   \brief Get the current buffered representation from the receiver's representation buffer

   On success the function fills in \p representation with the
   contents of the current representation buffer and returns
   CW_SUCCESS.

   On failure, it returns CW_FAILURE and sets errno to:
   ERANGE if not preceded by a cw_end_receive_tone() call, a prior
   successful cw_receive_representation call, or a prior
   cw_receive_buffer_dot or cw_receive_buffer_dash,
   EINVAL if the timestamp passed in is invalid,
   EAGAIN if the call is made too early to determine whether a
   complete representation has yet been placed in the buffer (that is,
   less than the end-of-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call). This is
   not a *hard* error, just an information that the caller should try
   to get the representation later.

   \p is_end_of_word indicates that the space after the last mark
   received is longer that the end-of-character gap, so it must be
   qualified as end-of-word gap.

   \p is_error indicates that the representation was terminated by an
   error condition.

   TODO: the function should be called cw_receiver_poll_representation().

   The function is called periodically (poll()-like function) by
   client code in hope that at some attempt receiver will be ready to
   pass \p representation. The attempt succeeds only if data stream is
   in "space" state. To mark end of the space, client code has to
   provide a timestamp (or pass NULL timestamp, the function will get
   time stamp at function call). The receiver needs to know the "end
   of space" event - thus the \p timestamp parameter.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends "end-of-character" gap or "end-of-word" gap
   \param representation - buffer for representation (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_representation(const struct timeval *timestamp,
			      /* out */ char *representation,
			      /* out */ bool *is_end_of_word,
			      /* out */ bool *is_error)
{
	int rv = cw_rec_poll_representation_internal(&cw_receiver,
						     timestamp,
						     representation,
						     is_end_of_word,
						     is_error);

	return rv;
}





/**
   \brief Get a current character

   Function returns the character currently stored in receiver's
   representation buffer.

   On success the function returns CW_SUCCESS, and fills \p c with the
   contents of the current representation buffer, translated into a
   character.

   On failure the function returns CW_FAILURE, with errno set to:

   ERANGE if not preceded by a cw_end_receive_tone() call, a prior
   successful cw_receive_character() call, or a
   cw_receive_buffer_dot() or cw_receive_buffer_dash() call,
   EINVAL if the timestamp passed in is invalid, or
   EAGAIN if the call is made too early to determine whether a
   complete character has yet been placed in the buffer (that is, less
   than the end-of-character gap period elapsed since the last
   cw_end_receive_tone() or cw_receive_buffer_dot/dash call).
   ENOENT if character stored in receiver cannot be recognized as valid

   \p is_end_of_word indicates that the space after the last mark
   received is longer that the end-of-character gap, so it must be
   qualified as end-of-word gap.

   \p is_error indicates that the character was terminated by an error
   condition.

   testedin::test_helper_receive_tests()

   \param timestamp - timestamp of event that ends end-of-character gap or end-of-word gap
   \param c - buffer for character (output parameter)
   \param is_end_of_word - buffer for "is end of word" state (output parameter)
   \param is_error - buffer for "error" state (output parameter)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_receive_character(const struct timeval *timestamp,
			 /* out */ char *c,
			 /* out */ bool *is_end_of_word,
			 /* out */ bool *is_error)
{
	int rv = cw_rec_poll_character_internal(&cw_receiver, timestamp, c, is_end_of_word, is_error);
	return rv;
}





/**
   \brief Clear receiver's representation buffer

   Clears the receiver's representation buffer, resets receiver's
   internal state. This prepares the receiver to receive marks and
   spaces again.

   This routine must be called after successful, or terminating,
   cw_receive_representation() or cw_receive_character() calls, to
   clear the states and prepare the buffer to receive more marks and spaces.
*/
void cw_clear_receive_buffer(void)
{
	cw_rec_clear_buffer_internal(&cw_receiver);

	return;
}





/**
   \brief Get the number of elements (dots/dashes) the receiver's buffer can accommodate

   The maximum number of elements written out by cw_receive_representation()
   is the capacity + 1, the extra character being used for the terminating
   NUL.

   \return number of elements that can be stored in receiver's representation buffer
*/
int cw_get_receive_buffer_capacity(void)
{
	return CW_REC_REPRESENTATION_CAPACITY;
}





/**
   \brief Get the number of elements (dots/dashes) currently pending in the cw_receiver's representation buffer

   testedin::test_helper_receive_tests()

   \return number of elements in receiver's representation buffer
*/
int cw_get_receive_buffer_length(void)
{
	return cw_rec_get_buffer_length_internal(&cw_receiver);
}





/**
   \brief Clear receive data

   Clear the receiver's representation buffer, statistics, and any
   retained receiver's state.  This function is suitable for calling
   from an application exit handler.
*/
void cw_reset_receive(void)
{
	cw_rec_reset_internal(&cw_receiver);

	return;
}





/* ******************************************************************** */
/*                                 Key                                  */
/* ******************************************************************** */





/**
   \brief Register external callback function for keying

   Register a \p callback_func function that should be called when a state
   of a key changes from "key open" to "key closed", or vice-versa.

   The first argument passed to the registered callback function is the
   supplied \p callback_arg, if any.  The second argument passed to
   registered callback function is the key state: CW_KEY_STATE_CLOSED
   (one/true) for "key closed", and CW_KEY_STATE_OPEN (zero/false) for
   "key open".

   Calling this routine with a NULL function address disables keying
   callbacks.  Any callback supplied will be called in signal handler
   context (??).

   \param callback_func - callback function to be called on key state changes
   \param callback_arg - first argument to callback_func
*/
void cw_register_keying_callback(void (*callback_func)(void*, int), void *callback_arg)
{
	cw_key_register_keying_callback_internal(&cw_key, callback_func, callback_arg);
	return;
}





/*
  Most of the time libcw just passes around key_callback_arg,
  not caring of what type it is, and not attempting to do any
  operations on it. On one occasion however, it needs to know whether
  key_callback_arg is of type 'struct timeval', and if so, it
  must do some operation on it. I could pass struct with ID as
  key_callback_arg, but that may break some old client
  code. Instead I've created this function that has only one, very
  specific purpose: to pass to libcw a pointer to timer.

  The timer is owned by client code, and is used to measure and clock
  iambic keyer.
*/
void cw_iambic_keyer_register_timer(struct timeval *timer)
{
	cw_key_ik_register_timer_internal(&cw_key, timer);
	return;
}





/**
   \brief Enable iambic Curtis mode B

   Normally, the iambic keying functions will emulate Curtis 8044 Keyer
   mode A.  In this mode, when both paddles are pressed together, the
   last dot or dash being sent on release is completed, and nothing else
   is sent. In mode B, when both paddles are pressed together, the last
   dot or dash being sent on release is completed, then an opposite
   element is also sent. Some operators prefer mode B, but timing is
   more critical in this mode. The default mode is Curtis mode A.
*/
void cw_enable_iambic_curtis_mode_b(void)
{
	cw_key_ik_enable_curtis_mode_b_internal(&cw_key);
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
void cw_disable_iambic_curtis_mode_b(void)
{
	cw_key_ik_disable_curtis_mode_b_internal(&cw_key);
	return;
}





/**
   See documentation of cw_enable_iambic_curtis_mode_b() for more information
*/
int cw_get_iambic_curtis_mode_b_state(void)
{
	return (int) cw_key_ik_get_curtis_mode_b_state_internal(&cw_key);
}





/**
   \brief Inform about changed state of iambic keyer's paddles

   Function informs the library that the iambic keyer paddles have
   changed state.  The new paddle states are recorded, and if either
   transition from false to true, paddle latches, for iambic functions,
   are also set.

   On success, the routine returns CW_SUCCESS.
   On failure, it returns CW_FAILURE, with errno set to EBUSY if the
   tone queue or straight key are using the sound card, console
   speaker, or keying system.

   If appropriate, this routine starts the keyer functions sending the
   relevant element.  Element send and timing occurs in the background,
   so this routine returns almost immediately.  See cw_keyer_element_wait()
   and cw_keyer_wait() for details about how to check the current status of
   iambic keyer background processing.

   testedin::test_keyer()

   \param dot_paddle_state
   \param dash_paddle_state

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_notify_keyer_paddle_event(int dot_paddle_state, int dash_paddle_state)
{
	return cw_key_ik_notify_paddle_event_internal(&cw_key, dot_paddle_state, dash_paddle_state);
}





/**
   \brief Change state of dot paddle

   Alter the state of just one of the two iambic keyer paddles.
   The other paddle state of the paddle pair remains unchanged.

   See cw_notify_keyer_paddle_event() for details of iambic keyer
   background processing, and how to check its status.

   \param dot_paddle_state
*/
int cw_notify_keyer_dot_paddle_event(int dot_paddle_state)
{
	return cw_notify_keyer_paddle_event(dot_paddle_state, cw_key.ik.dash_paddle);
}





/**
   See documentation of cw_notify_keyer_dot_paddle_event() for more information
*/
int cw_notify_keyer_dash_paddle_event(int dash_paddle_state)
{
	return cw_notify_keyer_paddle_event(cw_key.ik.dot_paddle, dash_paddle_state);
}





/**
   \brief Get the current saved states of the two paddles

   testedin::test_keyer()

   \param dot_paddle_state
   \param dash_paddle_state
*/
void cw_get_keyer_paddles(int *dot_paddle_state, int *dash_paddle_state)
{
	cw_key_ik_get_paddles_internal(&cw_key, dot_paddle_state, dash_paddle_state);
	return;
}





/**
   \brief Get the current states of paddle latches

   Function returns the current saved states of the two paddle latches.
   A paddle latches is set to true when the paddle state becomes true,
   and is cleared if the paddle state is false when the element finishes
   sending.

   \param dot_paddle_latch_state
   \param dash_paddle_latch_state
*/
void cw_get_keyer_paddle_latches(int *dot_paddle_latch_state, int *dash_paddle_latch_state)
{
	cw_key_ik_get_paddle_latches_internal(&cw_key, dot_paddle_latch_state, dash_paddle_latch_state);
	return;
}





/**
   \brief Check if a keyer is busy

   \return true if keyer is busy
   \return false if keyer is not busy
*/
bool cw_is_keyer_busy(void)
{
	return cw_key_ik_is_busy_internal(&cw_key);
}





/**
   \brief Wait for end of element from the keyer

   Waits until the end of the current element, dot or dash, from the keyer.

   On error the function returns CW_FAILURE, with errno set to
   EDEADLK if SIGALRM is blocked.

   testedin::test_keyer()

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer_element(void)
{
	return cw_key_ik_wait_for_element_internal(&cw_key);
}





/**
   \brief Wait for the current keyer cycle to complete

   The routine returns CW_SUCCESS on success.  On error, it returns
   CW_FAILURE, with errno set to EDEADLK if SIGALRM is blocked or if
   either paddle state is true.

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_wait_for_keyer(void)
{
	return cw_key_ik_wait_for_keyer_internal(&cw_key);
}





/**
   \brief Reset iambic keyer data

   Clear all latches and paddle states of iambic keyer, return to
   Curtis 8044 Keyer mode A, and return to silence.  This function is
   suitable for calling from an application exit handler.
*/
void cw_reset_keyer(void)
{
	cw_key_ik_reset_internal(&cw_key);
	return;
}





#if 0 /* unused */
/* Period of constant tone generation after which we need another timeout,
   to ensure that the soundcard doesn't run out of data. */
static const int STRAIGHT_KEY_TIMEOUT = 500000;





/**
   \brief Generate a tone while straight key is down

   Soundcard tone data is only buffered to last about a second on each
   cw_generate_sound_internal() call, and holding down the straight key
   for longer than this could cause a soundcard data underrun.  To guard
   against this, a timeout is generated every half-second or so while the
   straight key is down.  The timeout generates a chunk of sound data for
   the soundcard.
*/
void cw_straight_key_clock_internal(void)
{
	if (cw_straight_key->key_value == CW_KEY_STATE_CLOSED) {
		/* Generate a quantum of tone data, and request another
		   timeout. */
		// cw_generate_sound_internal();
		cw_timer_run_with_handler_internal(STRAIGHT_KEY_TIMEOUT, NULL);
	}

	return;
}
#endif





/**
   \brief Inform the library that the straight key has changed state

   This routine returns CW_SUCCESS on success.  On error, it returns CW_FAILURE,
   with errno set to EBUSY if the tone queue or iambic keyer are using
   the sound card, console speaker, or keying control system.  If
   \p key_state indicates no change of state, the call is ignored.

   \p key_state may be either CW_KEY_STATE_OPEN (false) or CW_KEY_STATE_CLOSED (true).

   testedin::test_straight_key()

   \param key_state - state of straight key
*/
int cw_notify_straight_key_event(int key_state)
{
	return cw_key_sk_notify_event_internal(&cw_key, key_state);
}





/**
   \brief Get saved state of straight key

   Returns the current saved state of the straight key.

   testedin::test_straight_key()

   \return CW_KEY_STATE_CLOSED (true) if the key is down
   \return CW_KEY_STATE_OPEN (false) if the key up
*/
int cw_get_straight_key_state(void)
{
	return cw_key_sk_get_state_internal(&cw_key);
}





/**
   \brief Check if the straight key is busy

   This routine is just a pseudonym for cw_get_straight_key_state(),
   and exists to fill a hole in the API naming conventions.

   testedin::test_straight_key()

   \return true if the straight key is busy
   \return false if the straight key is not busy
*/
bool cw_is_straight_key_busy(void)
{
	return cw_key_sk_is_busy_internal(&cw_key);
}





/**
   \brief Clear the straight key state, and return to silence

   This function is suitable for calling from an application exit handler.
*/
void cw_reset_straight_key(void)
{
	cw_key_sk_reset_internal(&cw_key);
	return;
}
