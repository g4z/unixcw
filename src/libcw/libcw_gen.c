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
   \file libcw_gen.c

   \brief Generate pcm samples according to tones from tone queue, and
   send them to audio sink.

   Functions operating on one of core elements of libcw: a generator.

   Generator is an object that has access to audio sink (soundcard,
   console buzzer, null audio device) and that can play dots and
   dashes using the audio sink.

   You can request generator to produce audio by using *_play_*()
   functions.

   The inner workings of the generator seem to be quite simple:
   1. dequeue tone from tone queue
   2. recalculate tone length in usecs into length in samples
   3. for every sample in tone, calculate sine wave sample and
      put it in generator's constant size buffer
   4. if buffer is full of sine wave samples, push it to audio sink
   5. since buffer is shorter than (almost) any tone, you will push
      the buffer multiple times per tone
   6. if you iterated over all samples in tone, but you still didn't
      fill up that last buffer, dequeue next tone from queue, go to #2
   7. if there are no more tones in queue, pad the buffer with silence,
      and push the buffer to audio sink.

   Looks simple, right? But it's the little details that ruin it all.
   One of the details is tone's slopes.
*/





#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h> /* uint32_t */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif





#include "libcw_gen.h"
#include "libcw_debug.h"
#include "libcw_utils.h"
#include "libcw_signal.h"
#include "libcw_data.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"





#ifndef M_PI  /* C99 may not define M_PI */
#define M_PI  3.14159265358979323846
#endif





/* From libcw_debug.c. */
extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;





/* Most of audio systems (excluding console) should be configured to
   have specific sample rate. Some audio systems (with connection with
   given hardware) can support several different sample rates. Values of
   supported sample rates are standardized. Here is a list of them to be
   used by this library.
   When the library configures given audio system, it tries if the system
   will accept a sample rate from the table, starting from the first one.
   If a sample rate is accepted, rest of sample rates is not tested anymore. */
const unsigned int cw_supported_sample_rates[] = {
	44100,
	48000,
	32000,
	22050,
	16000,
	11025,
	 8000,
	    0 /* guard */
};





/* Every audio system opens an audio device: a default device, or some
   other device. Default devices have their default names, and here is
   a list of them. It is indexed by values of "enum cw_audio_systems". */
static const char *default_audio_devices[] = {
	(char *) NULL,          /* CW_AUDIO_NONE */
	CW_DEFAULT_NULL_DEVICE, /* CW_AUDIO_NULL */
	CW_DEFAULT_CONSOLE_DEVICE,
	CW_DEFAULT_OSS_DEVICE,
	CW_DEFAULT_ALSA_DEVICE,
	CW_DEFAULT_PA_DEVICE,
	(char *) NULL }; /* just in case someone decided to index the table with CW_AUDIO_SOUNDCARD */





/* Generic constants - common for all audio systems (or not used in some of systems). */

static const long int CW_AUDIO_VOLUME_RANGE = (1 << 15);  /* 2^15 = 32768 */
static const int      CW_AUDIO_SLOPE_LEN = 5000;          /* Length of a single slope (rising or falling) in standard tone. [us] */

/* Shortest length of time (in microseconds) that is used by libcw for
   idle waiting and idle loops. If a libcw function needs to wait for
   something, or make an idle loop, it should call
   usleep(N * gen->quantum_len)

   This is also length of a single "forever" tone. */
static const int CW_AUDIO_QUANTUM_LEN_INITIAL = 100;  /* [us] */





static int   cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device);
static void *cw_gen_dequeue_and_play_internal(void *arg);
static int   cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone);
static int   cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone);
static int   cw_gen_write_to_soundcard_internal(cw_gen_t *gen, cw_tone_t *tone, int queue_rv);
static int   cw_gen_play_valid_character_internal(cw_gen_t *gen, char character, int partial);
static void  cw_gen_recalculate_slopes_internal(cw_gen_t *gen);





/**
   \brief Get a copy of readable label of current audio system

   Get a copy of human-readable string describing audio system
   associated currently with given \p gen.

   The function returns newly allocated pointer to one of following
   strings: "None", "Null", "Console", "OSS", "ALSA", "PulseAudio",
   "Soundcard".

   The returned pointer is owned by caller.

   Notice that the function returns a new pointer to newly allocated
   string. cw_generator_get_audio_system_label() returns a pointer to
   static string owned by library.

   \param gen - generator for which to check audio system label

   \return audio system's label
*/
char *cw_gen_get_audio_system_label_internal(cw_gen_t *gen)
{
	char *s = strdup(cw_get_audio_system_label(gen->audio_system));
	if (!s) {
		cw_vdm ("failed to strdup() audio system label for audio system %d\n", gen->audio_system);
	}

	return s;
}





/**
   \brief Start a generator
*/
int cw_gen_start_internal(cw_gen_t *gen)
{
	gen->phase_offset = 0.0;

	/* This should be set to true before launching
	   cw_gen_dequeue_and_play_internal(), because loop in the
	   function run only when the flag is set. */
	gen->do_dequeue_and_play = true;

	gen->client.thread_id = pthread_self();

	if (gen->audio_system == CW_AUDIO_NULL
	    || gen->audio_system == CW_AUDIO_CONSOLE
	    || gen->audio_system == CW_AUDIO_OSS
	    || gen->audio_system == CW_AUDIO_ALSA
	    || gen->audio_system == CW_AUDIO_PA) {

		/* cw_gen_dequeue_and_play_internal() is THE
		   function that does the main job of generating
		   tones. */
		int rv = pthread_create(&gen->thread.id, &gen->thread.attr,
					cw_gen_dequeue_and_play_internal,
					(void *) gen);
		if (rv != 0) {
			gen->do_dequeue_and_play = false;

			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: failed to create %s generator thread", cw_get_audio_system_label(gen->audio_system));
			return CW_FAILURE;
		} else {
			gen->thread.running = true;

			/* For some yet unknown reason you have to put
			   usleep() here, otherwise a generator may
			   work incorrectly */
			usleep(100000);
#ifdef LIBCW_WITH_DEV
			cw_dev_debug_print_generator_setup(gen);
#endif
			return CW_SUCCESS;
		}
	} else {
		gen->do_dequeue_and_play = false;

		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: unsupported audio system %d", gen->audio_system);
		return CW_FAILURE;
	}
}





/**
   \brief Set audio device name or path

   Set path to audio device, or name of audio device. The path/name
   will be associated with given generator \p gen, and used when opening
   audio device.

   Use this function only when setting up a generator.

   Function creates its own copy of input string.

   \param gen - generator to be updated
   \param device - device to be assigned to generator \p gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on errors
*/
int cw_gen_set_audio_device_internal(cw_gen_t *gen, const char *device)
{
	/* this should be NULL, either because it has been
	   initialized statically as NULL, or set to
	   NULL by generator destructor */
	assert (!gen->audio_device);
	assert (gen->audio_system != CW_AUDIO_NONE);

	if (gen->audio_system == CW_AUDIO_NONE) {
		gen->audio_device = (char *) NULL;
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: no audio system specified");
		return CW_FAILURE;
	}

	if (device) {
		gen->audio_device = strdup(device);
	} else {
		gen->audio_device = strdup(default_audio_devices[gen->audio_system]);
	}

	if (!gen->audio_device) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Silence the generator

   Force an audio sink currently used by generator \p gen to go
   silent.

   The function does not clear/flush tone queue, nor does it stop the
   generator. It just makes sure that audio sink (console / OSS / ALSA
   / PulseAudio) does not produce a sound of any frequency and any
   volume.

   You probably want to call cw_tq_flush_internal(gen->tq) before
   calling this function.

   \param gen - generator using an audio sink that should be silenced

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure to silence an audio sink
*/
int cw_gen_silence_internal(cw_gen_t *gen)
{
	if (!gen) {
		/* this may happen because the process of finalizing
		   usage of libcw is rather complicated; this should
		   be somehow resolved */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		return CW_SUCCESS;
	}

	if (!(gen->thread.running)) {
		/* Silencing a generator means enqueueing and playing
		   a tone with zero frequency.  We shouldn't do this
		   when a "dequeue-and-play-a-tone" function is not
		   running (anymore). This is not an error situation,
		   so return CW_SUCCESS. */
		return CW_SUCCESS;
	}

	int status = CW_SUCCESS;

	if (gen->audio_system == CW_AUDIO_NULL) {
		; /* pass */
	} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* sine wave generation should have been stopped
		   by a code generating dots/dashes, but
		   just in case... */
		cw_console_silence(gen);

	} else if (gen->audio_system == CW_AUDIO_OSS
		   || gen->audio_system == CW_AUDIO_ALSA
		   || gen->audio_system == CW_AUDIO_PA) {

		cw_tone_t tone;
		CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);

		/* allow some time for playing the last tone */
		usleep(2 * gen->quantum_len);
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: called silence() function for generator without audio system specified");
	}

	if (gen->audio_system == CW_AUDIO_ALSA) {
		/* "Stop a PCM dropping pending frames. " */
		cw_alsa_drop(gen);
	}

	//gen->do_dequeue_and_play = false;

	return status;
}





/**
   \brief Create new generator

   testedin::test_cw_gen_new_delete_internal()
*/
cw_gen_t *cw_gen_new_internal(int audio_system, const char *device)
{
#ifdef LIBCW_WITH_DEV
	fprintf(stderr, "libcw build %s %s\n", __DATE__, __TIME__);
#endif

	cw_assert (audio_system != CW_AUDIO_NONE, "can't create generator with audio system \"NONE\"");

	cw_gen_t *gen = (cw_gen_t *) malloc(sizeof (cw_gen_t));
	if (!gen) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
			      "libcw: malloc()");
		return (cw_gen_t *) NULL;
	}

	gen->tq = cw_tq_new_internal();
	if (!gen->tq) {
		cw_gen_delete_internal(&gen);
		return (cw_gen_t *) NULL;
	} else {
		/* Because libcw_tq.c/cw_tq_enqueue_internal()/pthread_kill(tq->gen->thread.id, SIGALRM); */
		gen->tq->gen = gen;
	}

	gen->audio_device = NULL;
	//gen->audio_system = audio_system;
	gen->audio_device_is_open = false;
	gen->dev_raw_sink = -1;


	/* Essential sending parameters. */
	gen->send_speed = CW_SPEED_INITIAL,
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->weighting = CW_WEIGHTING_INITIAL;


	gen->parameters_in_sync = false;


	gen->do_dequeue_and_play = false;


	gen->buffer = NULL;
	gen->buffer_n_samples = -1;

	gen->oss_version.x = -1;
	gen->oss_version.y = -1;
	gen->oss_version.z = -1;


	gen->client.name = (char *) NULL;

	gen->tone_slope.len = CW_AUDIO_SLOPE_LEN;
	gen->tone_slope.shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
	gen->tone_slope.amplitudes = NULL;
	gen->tone_slope.n_amplitudes = 0;

#ifdef LIBCW_WITH_PULSEAUDIO
	gen->pa_data.s = NULL;

	gen->pa_data.ba.prebuf    = (uint32_t) -1;
	gen->pa_data.ba.tlength   = (uint32_t) -1;
	gen->pa_data.ba.minreq    = (uint32_t) -1;
	gen->pa_data.ba.maxlength = (uint32_t) -1;
	gen->pa_data.ba.fragsize  = (uint32_t) -1;
#endif

	gen->open_device = NULL;
	gen->close_device = NULL;
	gen->write = NULL;

	pthread_attr_init(&gen->thread.attr);
	/* Thread must be joinable in order to make a safe call to
	   pthread_kill(thread_id, 0). pthreads are joinable by
	   default, but I take this explicit call as a good
	   opportunity to make this comment. */
	pthread_attr_setdetachstate(&gen->thread.attr, PTHREAD_CREATE_JOINABLE);
	gen->thread.running = false;


	gen->dot_len = 0;
	gen->dash_len = 0;
	gen->eom_space_len = 0;
	gen->eoc_space_len = 0;
	gen->eow_space_len = 0;

	gen->additional_space_len = 0;
	gen->adjustment_space_len = 0;


	gen->quantum_len = CW_AUDIO_QUANTUM_LEN_INITIAL;


	gen->buffer_sub_start = 0;
	gen->buffer_sub_stop  = 0;


	gen->key = (cw_key_t *) NULL;


	int rv = cw_gen_new_open_internal(gen, audio_system, device);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: failed to open audio device for audio system '%s' and device '%s'", cw_get_audio_system_label(audio_system), device);
		cw_gen_delete_internal(&gen);
		return (cw_gen_t *) NULL;
	}

	if (audio_system == CW_AUDIO_NULL
	    || audio_system == CW_AUDIO_CONSOLE) {

		; /* the two types of audio output don't require audio buffer */
	} else {
		gen->buffer = (cw_sample_t *) malloc(gen->buffer_n_samples * sizeof (cw_sample_t));
		if (!gen->buffer) {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_STDLIB, CW_DEBUG_ERROR,
				      "libcw: malloc()");
			cw_gen_delete_internal(&gen);
			return (cw_gen_t *) NULL;
		}
	}

	/* Set slope that late, because it uses value of sample rate.
	   The sample rate value is set in
	   cw_gen_new_open_internal(). */
	rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, CW_AUDIO_SLOPE_LEN);
	if (rv == CW_FAILURE) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: failed to set slope");
		cw_gen_delete_internal(&gen);
		return (cw_gen_t *) NULL;
	}

	cw_sigalrm_install_top_level_handler_internal();

	return gen;
}





/**
   \brief Delete a generator

   testedin::test_cw_gen_new_delete_internal()
*/
void cw_gen_delete_internal(cw_gen_t **gen)
{
	cw_assert (gen, "generator is NULL");

	if (!*gen) {
		return;
	}

	if ((*gen)->do_dequeue_and_play) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: you forgot to call cw_generator_stop()");
		cw_gen_stop_internal(*gen);
	}

	/* Wait for "write" thread to end accessing output
	   file descriptor. I have come up with value 500
	   after doing some experiments.

	   FIXME: magic number. I think that we can come up
	   with algorithm for calculating the value. */
	usleep(500);

	free((*gen)->audio_device);
	(*gen)->audio_device = NULL;

	free((*gen)->buffer);
	(*gen)->buffer = NULL;

	if ((*gen)->close_device) {
		(*gen)->close_device(*gen);
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: WARNING: NULL function pointer, something went wrong");
	}

	pthread_attr_destroy(&((*gen)->thread.attr));

	free((*gen)->client.name);
	(*gen)->client.name = NULL;

	free((*gen)->tone_slope.amplitudes);
	(*gen)->tone_slope.amplitudes = NULL;

	cw_tq_delete_internal(&((*gen)->tq));

	(*gen)->audio_system = CW_AUDIO_NONE;

	free(*gen);
	*gen = NULL;

	return;
}





/**
   \brief Stop a generator

   Empty generator's tone queue.
   Silence generator's audio sink.
   Stop generator' "dequeue and play" thread function.
   If the thread does not stop in one second, kill it.

   You have to use cw_gen_start_internal() if you want to enqueue and
   play tones with the same generator again.

   It seems that only silencing of generator's audio sink may fail,
   and this is when this function may return CW_FAILURE. Otherwise
   function returns CW_SUCCESS.

   \return CW_SUCCESS if all four actions completed (successfully)
   \return CW_FAILURE if any of the four actions failed (see note above)
*/
int cw_gen_stop_internal(cw_gen_t *gen)
{
	if (!gen) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING,
			      "libcw: called the function for NULL generator");
		/* Not really a runtime error, so return
		   CW_SUCCESS. */
		return CW_SUCCESS;
	}

	cw_tq_flush_internal(gen->tq);

	int rv = cw_gen_silence_internal(gen);
	if (rv != CW_SUCCESS) {
		return CW_FAILURE;
	}

	gen->do_dequeue_and_play = false;

	if (!gen->thread.running) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw: EXIT: seems that thread function was not started at all");

		/* Don't call pthread_kill() on non-initialized
		   thread.id. The generator wasn't even started, so
		   let's return CW_SUCCESS. */
		return CW_SUCCESS;
	}

	/* This is to wake up cw_signal_wait_internal() function that
	   may be waiting idle for signal in "while ()" loop in thread
	   function. */
	pthread_kill(gen->thread.id, SIGALRM);

	/* This piece of comment was put before code using
	   pthread_kill(), and may apply only to that version. But it
	   may turn out that it will be valid for code using
	   pthread_join() as well, so I'm keeping it for now.

	   "
	   Sleep a bit to postpone closing a device.  This way we can
	   avoid a situation when "do_dequeue_and_play" is set to false
	   and device is being closed while a new buffer is being
	   prepared, and while write() tries to write this new buffer
	   to already closed device.

	   Without this sleep(), writei() from ALSA library may
	   return "File descriptor in bad state" error - this
	   happened when writei() tried to write to closed ALSA
	   handle.

	   The delay also allows the generator function thread to stop
	   generating tone (or for tone queue to get out of CW_TQ_IDLE
	   state) and exit before we resort to killing generator
	   function thread.
	   "
	*/


#if 0 /* Old code using pthread_kill() instead of pthread_join(). */

	struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
	cw_nanosleep_internal(&req);

	/* Check if generator thread is still there.  Remember that
	   pthread_kill(id, 0) is unsafe for detached threads: if thread
	   has finished, the ID may be reused, and may be invalid at
	   this point. */
	rv = pthread_kill(gen->thread.id, 0);
	if (rv == 0) {
		/* thread function didn't return yet; let's help it a bit */
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: forcing exit of thread function");
		rv = pthread_kill(gen->thread.id, SIGKILL);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_WARNING, "libcw: EXIT: pthread_kill() returns %d/%s", rv, strerror(rv));
	} else {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw: EXIT: seems that thread function exited voluntarily");
	}

	gen->thread.running = false;
	return CW_SUCCESS;
#else



#define CW_DEBUG_TIMING_JOIN 1

#if CW_DEBUG_TIMING_JOIN   /* Debug code to measure how long it takes to join threads. */
	struct timeval before, after;
	gettimeofday(&before, NULL);
#endif



	rv = pthread_join(gen->thread.id, NULL);



#if CW_DEBUG_TIMING_JOIN   /* Debug code to measure how long it takes to join threads. */
	gettimeofday(&after, NULL);
	cw_debug_msg ((&cw_debug_object), CW_DEBUG_GENERATOR, CW_DEBUG_INFO, "libcw/gen: joining thread took %d us", cw_timestamp_compare_internal(&before, &after));
#endif



	if (rv == 0) {
		gen->thread.running = false;
		return CW_SUCCESS;
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR, "libcw/gen: failed to join threads: \"%s\"", strerror(rv));
		return CW_FAILURE;
	}
#endif
}





/**
  \brief Open audio system

  A wrapper for code trying to open audio device specified by
  \p audio_system.  Open audio system will be assigned to given
  generator. Caller can also specify audio device to use instead
  of a default one.

  \param gen - freshly created generator
  \param audio_system - audio system to open and assign to the generator
  \param device - name of audio device to be used instead of a default one

  \return CW_SUCCESS on success
  \return CW_FAILURE otherwise
*/
int cw_gen_new_open_internal(cw_gen_t *gen, int audio_system, const char *device)
{
	/* FIXME: this functionality is partially duplicated in
	   src/cwutils/cw_common.c/cw_generator_new_from_config() */

	/* This function deliberately checks all possible values of
	   audio system name in separate 'if' clauses before it gives
	   up and returns CW_FAILURE. PA/OSS/ALSA are combined with
	   SOUNDCARD, so I have to check all three of them (because \p
	   audio_system may be set to SOUNDCARD). And since I check
	   the three in separate 'if' clauses, I can check all other
	   values of audio system as well. */

	if (audio_system == CW_AUDIO_NULL) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_NULL];
		if (cw_is_null_possible(dev)) {
			cw_null_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_PA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_PA];
		if (cw_is_pa_possible(dev)) {
			cw_pa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_OSS
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_OSS];
		if (cw_is_oss_possible(dev)) {
			cw_oss_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_ALSA
	    || audio_system == CW_AUDIO_SOUNDCARD) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_ALSA];
		if (cw_is_alsa_possible(dev)) {
			cw_alsa_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	if (audio_system == CW_AUDIO_CONSOLE) {

		const char *dev = device ? device : default_audio_devices[CW_AUDIO_CONSOLE];
		if (cw_is_console_possible(dev)) {
			cw_console_configure(gen, dev);
			return gen->open_device(gen);
		}
	}

	/* there is no next audio system type to try */
	return CW_FAILURE;
}





/**
   \brief Dequeue tones and push them to audio output

   Function dequeues tones from tone queue associated with generator
   and then sends them to preconfigured audio output (soundcard, NULL
   or console).

   Function dequeues tones (or waits for new tones in queue) and
   pushes them to audio output as long as
   generator->do_dequeue_and_play is true.

   The generator must be fully configured before calling this
   function.

   \param arg - generator (casted to (void *)) to be used for generating tones

   \return NULL pointer
*/
void *cw_gen_dequeue_and_play_internal(void *arg)
{
	cw_gen_t *gen = (cw_gen_t *) arg;

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, 0, CW_SLOPE_MODE_STANDARD_SLOPES);

	while (gen->do_dequeue_and_play) {
		int tq_rv = cw_tq_dequeue_internal(gen->tq, &tone);
		if (tq_rv == CW_TQ_NDEQUEUED_IDLE) {

			/* Tone queue has been totally drained with
			   previous call to dequeue(). No point in
			   making next iteration of while() and
			   calling the function again. So don't call
			   it, wait for signal from enqueue() function
			   informing that a new tone appeared in tone
			   queue. */

			/* A SIGALRM signal may also come from
			   cw_gen_stop_internal() that gently asks
			   this function to stop idling and nicely
			   return. */

			/* TODO: can we / should we specify on which
			   signal exactly we are waiting for? */
			cw_signal_wait_internal();
			continue;
		}


		cw_key_ik_increment_timer_internal(gen->key, tone.len);

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_HIGH : CW_DEBUG_EVENT_TONE_LOW);
#endif

		if (gen->audio_system == CW_AUDIO_NULL) {
			cw_null_write(gen, &tone);
		} else if (gen->audio_system == CW_AUDIO_CONSOLE) {
			cw_console_write(gen, &tone);
		} else {
			cw_gen_write_to_soundcard_internal(gen, &tone, tq_rv);
		}

		/*
		  When sending text from text input, the signal:
		   - allows client code to observe moment when state of tone
		     queue is "low/critical"; client code then can add more
		     characters to the queue; the observation is done using
		     cw_wait_for_tone_queue_critical();
		   - ...

		 */
		pthread_kill(gen->client.thread_id, SIGALRM);

		/* Generator may be used by iambic keyer to measure
		   periods of time (lengths of Mark and Space) - this
		   is achieved by enqueueing Marks and Spaces by keyer
		   in generator.

		   At this point the generator has finished generating
		   a tone of specified length. A duration of Mark or
		   Space has elapsed. Inform iambic keyer that the
		   tone it has enqueued has elapsed.

		   (Whether iambic keyer has enqueued any tones or
		   not, and whether it is waiting for the
		   notification, is a different story. We will let the
		   iambic keyer function called below to decide what
		   to do with the notification. If keyer is in idle
		   graph state, it will ignore the notification.)

		   Notice that this mechanism is needed only for
		   iambic keyer. Inner workings of straight key are
		   much more simple, the straight key doesn't need to
		   use generator as a timer. */
		if (!cw_key_ik_update_graph_state_internal(gen->key)) {
			/* just try again, once */
			usleep(1000);
			cw_key_ik_update_graph_state_internal(gen->key);
		}

#ifdef LIBCW_WITH_DEV
		cw_debug_ev ((&cw_debug_object_ev), 0, tone.frequency ? CW_DEBUG_EVENT_TONE_LOW : CW_DEBUG_EVENT_TONE_HIGH);
#endif

	} /* while (gen->do_dequeue_and_play) */

	cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_INFO,
		      "libcw: EXIT: generator stopped (gen->do_dequeue_and_play = %d)", gen->do_dequeue_and_play);

	/* Some functions in client thread may be waiting for the last
	   SIGALRM from the generator thread to continue/finalize their
	   business. Let's send the SIGALRM right before exiting. */

	/* This small delay before sending signal turns out to be helpful.

	   TODO: this is one of most mysterious comments in this code
	   base. What was I thinking? */
	struct timespec req = { .tv_sec = 0, .tv_nsec = CW_NSECS_PER_SEC / 2 };
	cw_nanosleep_internal(&req);

	pthread_kill(gen->client.thread_id, SIGALRM);
	gen->thread.running = false;
	return NULL;
}





/**
   \brief Calculate a fragment of sine wave

   Calculate a fragment of sine wave, as many samples as can be fitted
   in generator buffer's subarea.

   There will be (gen->buffer_sub_stop - gen->buffer_sub_start + 1)
   samples calculated and put into gen->buffer[], starting from
   gen->buffer[gen->buffer_sub_start].

   The function takes into account all state variables from gen,
   so initial phase of new fragment of sine wave in the buffer matches
   ending phase of a sine wave generated in previous call.

   \param gen - generator that generates sine wave
   \param tone - generated tone

   \return number of calculated samples
*/
int cw_gen_calculate_sine_wave_internal(cw_gen_t *gen, cw_tone_t *tone)
{
	assert (gen->buffer_sub_stop <= gen->buffer_n_samples);

	/* We need two separate iterators to correctly generate sine wave:
	    -- i -- for iterating through output buffer (generator
	            buffer's subarea), it can travel between buffer
	            cells delimited by start and stop (inclusive);
	    -- t -- for calculating phase of a sine wave; 't' always has to
	            start from zero for every calculated subarea (i.e. for
		    every call of this function);

	  Initial/starting phase of generated fragment is always retained
	  in gen->phase_offset, it is the only "memory" of previously
	  calculated fragment of sine wave (to be precise: it stores phase
	  of last sample in previously calculated fragment).
	  Therefore iterator used to calculate phase of sine wave can't have
	  the memory too. Therefore it has to always start from zero for
	  every new fragment of sine wave. Therefore a separate t. */

	double phase = 0.0;
	int t = 0;

	for (int i = gen->buffer_sub_start; i <= gen->buffer_sub_stop; i++) {
		phase = (2.0 * M_PI
				* (double) tone->frequency * (double) t
				/ (double) gen->sample_rate)
			+ gen->phase_offset;
		int amplitude = cw_gen_calculate_amplitude_internal(gen, tone);

		gen->buffer[i] = amplitude * sin(phase);

		tone->sample_iterator++;

		t++;
	}

	phase = (2.0 * M_PI
		 * (double) tone->frequency * (double) t
		 / (double) gen->sample_rate)
		+ gen->phase_offset;

	/* "phase" is now phase of the first sample in next fragment to be
	   calculated.
	   However, for long fragments this can be a large value, well
	   beyond <0; 2*Pi) range.
	   The value of phase may further accumulate in different
	   calculations, and at some point it may overflow. This would
	   result in an audible click.

	   Let's bring back the phase from beyond <0; 2*Pi) range into the
	   <0; 2*Pi) range, in other words lets "normalize" it. Or, in yet
	   other words, lets apply modulo operation to the phase.

	   The normalized phase will be used as a phase offset for next
	   fragment (during next function call). It will be added phase of
	   every sample calculated in next function call. */

	int n_periods = floor(phase / (2.0 * M_PI));
	gen->phase_offset = phase - n_periods * 2.0 * M_PI;

	return t;
}





/**
   \brief Calculate value of a single sample of sine wave

   This function calculates an amplitude (a value) of a single sample
   in sine wave PCM data.

   Actually "calculation" is a bit too big word. The function is just
   a three-level-deep decision tree, deciding which of precalculated
   values to return. There are no complicated arithmetical
   calculations being made each time the function is called, so the
   execution time should be pretty small.

   The precalcuated values depend on some factors, so the values
   should be re-calculated each time these factors change. See
   cw_generator_set_tone_slope() for list of these factors.

   A generator stores some of information needed to get an amplitude
   of every sample in a sine wave - this is why we have \p gen.  If
   tone's slopes are non-rectangular, the length of slopes is defined
   in generator. If a tone is non-silent, the volume is also defined
   in generator.

   However, decision tree for getting the amplitude also depends on
   some parameters that are strictly bound to tone, such as what is
   the shape of slopes for a given tone - this is why we have \p tone.
   The \p also stores iterator of samples - this is how we know for
   which sample to calculate the amplitude.

   \param gen - generator used to generate a sine wave
   \param tone - tone being generated

   \return value of a sample of sine wave, a non-negative number
*/
int cw_gen_calculate_amplitude_internal(cw_gen_t *gen, cw_tone_t *tone)
{
#if 0
	int amplitude = 0;
	/* Blunt algorithm for calculating amplitude;
	   for debug purposes only. */
	if (tone->frequency) {
		amplitude = gen->volume_abs;
	} else {
		amplitude = 0;
	}

	return amplitude;
#else

	if (tone->frequency <= 0) {
		return 0;
	}


	int amplitude = 0;

	/* Every tone, regardless of slope mode (CW_SLOPE_MODE_*), has
	   three components. It has rising slope + plateau + falling
	   slope.

	   There can be four variants of rising and falling slope
	   length, just as there are four CW_SLOPE_MODE_* values.

	   There can be also tones with zero-length plateau, and there
	   can be also tones with zero-length slopes. */

	if (tone->sample_iterator < tone->rising_slope_n_samples) {
		/* Beginning of tone, rising slope. */
		int i = tone->sample_iterator;
		amplitude = gen->tone_slope.amplitudes[i];
		assert (amplitude >= 0);

	} else if (tone->sample_iterator >= tone->rising_slope_n_samples
		   && tone->sample_iterator < tone->n_samples - tone->falling_slope_n_samples) {

		/* Middle of tone, plateau, constant amplitude. */
		amplitude = gen->volume_abs;
		assert (amplitude >= 0);

	} else if (tone->sample_iterator >= tone->n_samples - tone->falling_slope_n_samples) {
		/* Falling slope. */
		int i = tone->n_samples - tone->sample_iterator - 1;
		assert (i >= 0);
		amplitude = gen->tone_slope.amplitudes[i];
		assert (amplitude >= 0);

	} else {
		cw_assert (0, "->sample_iterator out of bounds:\n"
			   "tone->sample_iterator: %d\n"
			   "tone->n_samples: %"PRId64"\n"
			   "tone->rising_slope_n_samples: %d\n"
			   "tone->falling_slope_n_samples: %d\n",
			   tone->sample_iterator,
			   tone->n_samples,
			   tone->rising_slope_n_samples,
			   tone->falling_slope_n_samples);
	}

	assert (amplitude >= 0);
	return amplitude;
#endif
}





/**
   \brief Set parameters of tones generated by generator

   Most of variables related to slope of tones is in tone data type,
   but there are still some variables that are generator-specific, as
   they are common for all tones.  This function sets two of these
   variables.


   A: If you pass to function conflicting values of \p slope_shape and
   \p slope_len, the function will return CW_FAILURE. These
   conflicting values are rectangular slope shape and larger than zero
   slope length. You just can't have rectangular slopes that have
   non-zero length.


   B: If you pass to function '-1' as value of both \p slope_shape and
   \p slope_len, the function won't change any of the related two
   generator's parameters.


   C1: If you pass to function '-1' as value of either \p slope_shape
   or \p slope_len, the function will attempt to set only this
   generator's parameter that is different than '-1'.

   C2: However, if selected slope shape is rectangular, function will
   set generator's slope length to zero, even if value of \p
   slope_len is '-1'.


   D: Notice that the function allows non-rectangular slope shape with
   zero length of the slopes. The slopes will be non-rectangular, but
   just unusually short.


   The function should be called every time one of following
   parameters change:

   \li shape of slope,
   \li length of slope,
   \li generator's sample rate,
   \li generator's volume.

   There are four supported shapes of slopes:
   \li linear (the only one supported by libcw until version 4.1.1),
   \li raised cosine (supposedly the most desired shape),
   \li sine,
   \li rectangular.

   Use CW_TONE_SLOPE_SHAPE_* symbolic names as values of \p slope_shape.

   FIXME: first argument of this public function is gen, but no
   function provides access to generator variable.

   \param gen - generator for which to set tone slope parameters
   \param slope_shape - shape of slope: linear, raised cosine, sine, rectangular
   \param slope_len - length of slope [microseconds]

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_generator_set_tone_slope(cw_gen_t *gen, int slope_shape, int slope_len)
{
	assert (gen);

	/* Handle conflicting values of arguments. */
	if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR
	    && slope_len > 0) {

		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: requested a rectangular slope shape, but also requested slope len > 0");

		return CW_FAILURE;
	}

	/* Assign new values from arguments. */
	if (slope_shape != -1) {
		gen->tone_slope.shape = slope_shape;
	}
	if (slope_len != -1) {
		gen->tone_slope.len = slope_len;
	}


	/* Override of slope length. */
	if (slope_shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
		gen->tone_slope.len = 0;
	}


	int slope_n_samples = ((gen->sample_rate / 100) * gen->tone_slope.len) / 10000;
	cw_assert (slope_n_samples >= 0, "negative slope_n_samples: %d", slope_n_samples);


	/* Reallocate the table of slope amplitudes only when necessary.

	   In practice the function will be called foremost when user
	   changes volume of tone (and then the function may be
	   called several times in a row if volume is changed in
	   steps). In such situation the size of amplitudes table
	   doesn't change. */

	if (gen->tone_slope.n_amplitudes != slope_n_samples) {

		 /* Remember that slope_n_samples may be zero. In that
		    case realloc() would equal to free(). We don't
		    want to have NULL ->amplitudes, so don't modify
		    ->amplitudes for zero-length slopes.  Since with
		    zero-length slopes we won't be referring to
		    ->amplitudes[], it is ok that the table will not
		    be up-to-date. */

		if (slope_n_samples > 0) {
			gen->tone_slope.amplitudes = realloc(gen->tone_slope.amplitudes, sizeof(float) * slope_n_samples);
			if (!gen->tone_slope.amplitudes) {
				cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
					      "libcw: failed to realloc() table of slope amplitudes");
				return CW_FAILURE;
			}
		}

		gen->tone_slope.n_amplitudes = slope_n_samples;
	}

	cw_gen_recalculate_slopes_internal(gen);

	return CW_SUCCESS;
}





/**
   \brief Recalculate amplitudes of PCM samples that form tone's slopes

   TODO: consider writing unit test code for the function.

   \param gen - generator
*/
void cw_gen_recalculate_slopes_internal(cw_gen_t *gen)
{
	/* The values in amplitudes[] change from zero to max (at
	   least for any sane slope shape), so naturally they can be
	   used in forming rising slope. However they can be used in
	   forming falling slope as well - just iterate the table from
	   end to beginning. */
	for (int i = 0; i < gen->tone_slope.n_amplitudes; i++) {

		if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_LINEAR) {
			gen->tone_slope.amplitudes[i] = 1.0 * gen->volume_abs * i / gen->tone_slope.n_amplitudes;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_SINE) {
			float radian = i * (M_PI / 2.0) / gen->tone_slope.n_amplitudes;
			gen->tone_slope.amplitudes[i] = sin(radian) * gen->volume_abs;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE) {
			float radian = i * M_PI / gen->tone_slope.n_amplitudes;
			gen->tone_slope.amplitudes[i] = (1 - ((1 + cos(radian)) / 2)) * gen->volume_abs;

		} else if (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR) {
			/* CW_TONE_SLOPE_SHAPE_RECTANGULAR is covered
			   before entering this "for" loop. */
			cw_assert (0, "we shouldn't be here, calculating rectangular slopes");

		} else {
			cw_assert (0, "unsupported slope shape %d", gen->tone_slope.shape);
		}
	}

	return;
}





/**
   \brief Write tone to soundcard
*/
int cw_gen_write_to_soundcard_internal(cw_gen_t *gen, cw_tone_t *tone, int queue_rv)
{
	assert (queue_rv != CW_TQ_NDEQUEUED_IDLE);

	if (queue_rv == CW_TQ_NDEQUEUED_EMPTY) {
		/* All tones have been already dequeued from tone
		   queue.

		   \p tone does not represent a valid tone to play. At
		   first sight there is no need to write anything to
		   soundcard. But...

		   It may happen that during previous call to the
		   function there were too few samples in a tone to
		   completely fill a buffer (see #needmoresamples tag
		   below).

		   We need to fill the buffer until it is full and
		   ready to be sent to audio sink.

		   Since there are no new tones for which we could
		   generate samples, we need to generate silence
		   samples.

		   Padding the buffer with silence seems to be a good
		   idea (it will work regardless of value (Mark/Space)
		   of last valid tone). We just need to know how many
		   samples of the silence to produce.

		   Number of these samples will be stored in
		   samples_to_write. */

		/* We don't have a valid tone, so let's construct a
		   fake one for purposes of padding. */

		/* Required length of padding space is from end of
		   last buffer subarea to end of buffer. */
		tone->n_samples = gen->buffer_n_samples - (gen->buffer_sub_stop + 1);;

		tone->len = 0;         /* This value matters no more, because now we only deal with samples. */
		tone->frequency = 0;   /* This fake tone is a piece of silence. */

		/* The silence tone used for padding doesn't require
		   any slopes. A slope falling to silence has been
		   already provided by last non-fake and non-silent
		   tone. */
		tone->slope_mode = CW_SLOPE_MODE_NO_SLOPES;
		tone->rising_slope_n_samples = 0;
		tone->falling_slope_n_samples = 0;

		tone->sample_iterator = 0;

		//fprintf(stderr, "++++ length of padding silence = %d [samples]\n", tone->n_samples);

	} else { /* tq_rv == CW_TQ_DEQUEUED */

		/* Recalculate tone parameters from microseconds into
		   samples. After this point the samples will be all
		   that matters. */

		/* 100 * 10000 = 1.000.000 usecs per second. */
		tone->n_samples = gen->sample_rate / 100;
		tone->n_samples *= tone->len;
		tone->n_samples /= 10000;

		//fprintf(stderr, "++++ length of regular tone = %d [samples]\n", tone->n_samples);

		/* Length of a single slope (rising or falling). */
		int slope_n_samples= gen->sample_rate / 100;
		slope_n_samples *= gen->tone_slope.len;
		slope_n_samples /= 10000;

		if (tone->slope_mode == CW_SLOPE_MODE_RISING_SLOPE) {
			tone->rising_slope_n_samples = slope_n_samples;
			tone->falling_slope_n_samples = 0;

		} else if (tone->slope_mode == CW_SLOPE_MODE_FALLING_SLOPE) {
			tone->rising_slope_n_samples = 0;
			tone->falling_slope_n_samples = slope_n_samples;

		} else if (tone->slope_mode == CW_SLOPE_MODE_STANDARD_SLOPES) {
			tone->rising_slope_n_samples = slope_n_samples;
			tone->falling_slope_n_samples = slope_n_samples;

		} else if (tone->slope_mode == CW_SLOPE_MODE_NO_SLOPES) {
			tone->rising_slope_n_samples = 0;
			tone->falling_slope_n_samples = 0;

		} else {
			cw_assert (0, "unknown tone slope mode %d", tone->slope_mode);
		}

		tone->sample_iterator = 0;
	}


	/* Total number of samples to write in a loop below. */
	int64_t samples_to_write = tone->n_samples;

#if 0
	fprintf(stderr, "++++ entering loop, tone->frequency = %d, buffer->n_samples = %d, tone->n_samples = %d, samples_to_write = %d\n",
		tone->frequency, gen->buffer_n_samples, tone->n_samples, samples_to_write);
	fprintf(stderr, "++++ entering loop, expected ~%f loops\n", 1.0 * samples_to_write / gen->buffer_n_samples);
	int debug_loop = 0;
#endif


	// cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "libcw: %lld samples, %d us, %d Hz", tone->n_samples, tone->len, gen->frequency);
	while (samples_to_write > 0) {

		int64_t free_space = gen->buffer_n_samples - gen->buffer_sub_start;
		if (samples_to_write > free_space) {
			/* There will be some tone samples left for
			   next iteration of this loop.  But this
			   buffer will be ready to be pushed to audio
			   sink. */
			gen->buffer_sub_stop = gen->buffer_n_samples - 1;
		} else if (samples_to_write == free_space) {
			/* How nice, end of tone samples aligns with
			   end of buffer (last sample of tone will be
			   placed in last cell of buffer).

			   But the result is the same - a full buffer
			   ready to be pushed to audio sink. */
			gen->buffer_sub_stop = gen->buffer_n_samples - 1;
		} else {
			/* There will be too few samples to fill a
			   buffer. We can't send an unready buffer to
			   audio sink. We will have to somehow pad the
			   buffer. */
			gen->buffer_sub_stop = gen->buffer_sub_start + samples_to_write - 1;
		}

		/* How many samples of audio buffer's subarea will be
		   calculated in a given cycle of "calculate sine
		   wave" code? */
		int buffer_sub_n_samples = gen->buffer_sub_stop - gen->buffer_sub_start + 1;


#if 0
		fprintf(stderr, "++++        loop #%d, buffer_sub_n_samples = %d\n", ++debug_loop, buffer_sub_n_samples);
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG,
			      "libcw: sub start: %d, sub stop: %d, sub len: %d, to calculate: %d", gen->buffer_sub_start, gen->buffer_sub_stop, buffer_sub_n_samples, samples_to_write);
#endif


		int calculated = cw_gen_calculate_sine_wave_internal(gen, tone);
		cw_assert (calculated == buffer_sub_n_samples,
			   "calculated wrong number of samples: %d != %d",
			   calculated, buffer_sub_n_samples);


		if (gen->buffer_sub_stop == gen->buffer_n_samples - 1) {

			/* We have a buffer full of samples. The
			   buffer is ready to be pushed to audio
			   sink. */
			gen->write(gen);
			gen->buffer_sub_start = 0;
			gen->buffer_sub_stop = 0;
#if CW_DEV_RAW_SINK
			cw_dev_debug_raw_sink_write_internal(gen);
#endif
		} else {
			/* #needmoresamples
			   There is still some space left in the
			   buffer, go fetch new tone from tone
			   queue. */

			gen->buffer_sub_start = gen->buffer_sub_stop + 1;

			cw_assert (gen->buffer_sub_start <= gen->buffer_n_samples - 1,
				   "sub start out of range: sub start = %d, buffer n samples = %d",
				   gen->buffer_sub_start, gen->buffer_n_samples);
		}

		samples_to_write -= buffer_sub_n_samples;

#if 0
		if (samples_to_write < 0) {
			cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_DEBUG, "samples left = %d", samples_to_write);
		}
#endif

	} /* while (samples_to_write > 0) { */

	//fprintf(stderr, "++++ left loop, %d loops, samples left = %d\n", debug_loop, (int) samples_to_write);

	return 0;
}





/**
   \brief Set sending speed of generator

   See libcw.h/CW_SPEED_{INITIAL|MIN|MAX} for initial/minimal/maximal value
   of send speed.

   errno is set to EINVAL if \p new_value is out of range.

   testedin::test_parameter_ranges()

   \param gen - generator for which to set the speed
   \param new_value - new value of send speed to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_speed_internal(cw_gen_t *gen, int new_value)
{
	if (new_value < CW_SPEED_MIN || new_value > CW_SPEED_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->send_speed) {
		gen->send_speed = new_value;

		/* Changes of send speed require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}





/**
   \brief Set frequency of generator

   Set frequency of sound wave generated by generator.
   The frequency must be within limits marked by CW_FREQUENCY_MIN
   and CW_FREQUENCY_MAX.

   See libcw.h/CW_FREQUENCY_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of frequency.

   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set new frequency
   \param new_value - new value of frequency to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_frequency_internal(cw_gen_t *gen, int new_value)
{
	if (new_value < CW_FREQUENCY_MIN || new_value > CW_FREQUENCY_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		gen->frequency = new_value;
		return CW_SUCCESS;
	}
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

   \param gen - generator for which to set a volume level
   \param new_value - new value of volume to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_volume_internal(cw_gen_t *gen, int new_value)
{
	if (new_value < CW_VOLUME_MIN || new_value > CW_VOLUME_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	} else {
		gen->volume_percent = new_value;
		gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;

		cw_generator_set_tone_slope(gen, -1, -1);

		return CW_SUCCESS;
	}
}





/**
   \brief Set sending gap of generator

   See libcw.h/CW_GAP_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of gap.
   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set gap
   \param new_value - new value of gap to be assigned to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_gap_internal(cw_gen_t *gen, int new_value)
{
	if (new_value < CW_GAP_MIN || new_value > CW_GAP_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->gap) {
		gen->gap = new_value;
		/* Changes of gap require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}





/**
   \brief Set sending weighting for generator

   See libcw.h/CW_WEIGHTING_{INITIAL|MIN|MAX} for initial/minimal/maximal
   value of weighting.
   errno is set to EINVAL if \p new_value is out of range.

   \param gen - generator for which to set new weighting
   \param new_value - new value of weighting to be assigned for generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_set_weighting_internal(cw_gen_t *gen, int new_value)
{
	if (new_value < CW_WEIGHTING_MIN || new_value > CW_WEIGHTING_MAX) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	if (new_value != gen->weighting) {
		gen->weighting = new_value;

		/* Changes of weighting require resynchronization. */
		gen->parameters_in_sync = false;
		cw_gen_sync_parameters_internal(gen);
	}

	return CW_SUCCESS;
}





/**
   \brief Get sending speed from generator

   \param gen - generator from which to get the parameter

   \return current value of the generator's send speed
*/
int cw_gen_get_speed_internal(cw_gen_t *gen)
{
	return gen->send_speed;
}





/**
   \brief Get frequency from generator

   Function returns "frequency" parameter of generator,
   even if the generator is stopped, or volume of generated sound is zero.

   \param gen - generator from which to get the parameter

   \return current value of generator's frequency
*/
int cw_gen_get_frequency_internal(cw_gen_t *gen)
{
	return gen->frequency;
}





/**
   \brief Get sound volume from generator

   Function returns "volume" parameter of generator,
   even if the generator is stopped.

   \param gen - generator from which to get the parameter

   \return current value of generator's sound volume
*/
int cw_gen_get_volume_internal(cw_gen_t *gen)
{
	return gen->volume_percent;
}








/**
   \brief Get sending gap from generator

   \param gen - generator from which to get the parameter

   \return current value of generator's sending gap
*/
int cw_gen_get_gap_internal(cw_gen_t *gen)
{
	return gen->gap;
}





/**
   \brief Get sending weighting from generator

   \param gen - generator from which to get the parameter

   \return current value of generator's sending weighting
*/
int cw_gen_get_weighting_internal(cw_gen_t *gen)
{
	return gen->weighting;
}





/**
   \brief Get timing parameters for sending

   Return the low-level timing parameters calculated from the speed, gap,
   tolerance, and weighting set.  Parameter values are returned in
   microseconds.

   Use NULL for the pointer argument to any parameter value not required.

   \param gen
   \param dot_len
   \param dash_len
   \param eom_space_len
   \param eoc_space_len
   \param eow_space_len
   \param additional_space_len
   \param adjustment_space_len
*/
void cw_gen_get_send_parameters_internal(cw_gen_t *gen,
					 int *dot_len,
					 int *dash_len,
					 int *eom_space_len,
					 int *eoc_space_len,
					 int *eow_space_len,
					 int *additional_space_len, int *adjustment_space_len)
{
	cw_gen_sync_parameters_internal(gen);

	if (dot_len)   *dot_len = gen->dot_len;
	if (dash_len)  *dash_len = gen->dash_len;

	if (eom_space_len)   *eom_space_len = gen->eom_space_len;
	if (eoc_space_len)   *eoc_space_len = gen->eoc_space_len;
	if (eow_space_len)   *eow_space_len = gen->eow_space_len;

	if (additional_space_len)    *additional_space_len = gen->additional_space_len;
	if (adjustment_space_len)    *adjustment_space_len = gen->adjustment_space_len;

	return;
}





/**
   \brief Play a mark (Dot or Dash)

   Low level primitive to play a tone for mark of the given type, followed
   by the standard inter-mark space.

   Function sets errno to EINVAL if an argument is invalid, and
   returns CW_FAILURE.

   Function also returns CW_FAILURE if adding the element to queue of
   tones failed.

   \param gen - generator to be used to play a mark and inter-mark space
   \param mark - mark to send: Dot (CW_DOT_REPRESENTATION) or Dash (CW_DASH_REPRESENTATION)

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_play_mark_internal(cw_gen_t *gen, char mark)
{
	int status;

	/* Synchronize low-level timings if required. */
	cw_gen_sync_parameters_internal(gen);
	/* TODO: do we need to synchronize here receiver as well? */

	/* Send either a dot or a dash mark, depending on representation. */
	if (mark == CW_DOT_REPRESENTATION) {
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->dot_len, CW_SLOPE_MODE_STANDARD_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);
	} else if (mark == CW_DASH_REPRESENTATION) {
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->dash_len, CW_SLOPE_MODE_STANDARD_SLOPES);
		status = cw_tq_enqueue_internal(gen->tq, &tone);
	} else {
		errno = EINVAL;
		status = CW_FAILURE;
	}

	if (!status) {
		return CW_FAILURE;
	}

	/* Send the inter-mark space. */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);
	if (!cw_tq_enqueue_internal(gen->tq, &tone)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Play end-of-character space

   The function plays space of length 2 Units. The function is
   intended to be used after inter-mark space has already been played.

   In such situation standard inter-mark space (one Unit) and
   end-of-character space (two Units) form a full standard
   end-of-character space (three Units).

   Inter-character adjustment space is added at the end.

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_eoc_space_internal(cw_gen_t *gen)
{
	/* Synchronize low-level timing parameters. */
	cw_gen_sync_parameters_internal(gen);

	/* Delay for the standard end of character period, plus any
	   additional inter-character gap */
	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eoc_space_len + gen->additional_space_len, CW_SLOPE_MODE_NO_SLOPES);
	return cw_tq_enqueue_internal(gen->tq, &tone);
}





/**
   \brief Play end-of-word space

   The function should be used to play a regular ' ' character.

   The function plays space of length 5 Units. The function is
   intended to be used after inter-mark space and end-of-character
   space have already been played.

   In such situation standard inter-mark space (one Unit) and
   end-of-character space (two Units) and end-of-word space (five
   units) form a full standard end-of-word space (seven Units).

   Inter-word adjustment space is added at the end.

   \param gen

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_eow_space_internal(cw_gen_t *gen)
{
	/* Synchronize low-level timing parameters. */
	cw_gen_sync_parameters_internal(gen);

	/* Send silence for the word delay period, plus any adjustment
	   that may be needed at end of word. Make it in two tones,
	   and here is why.

	   Let's say that 'tone queue low watermark' is one element
	   (i.e. one tone).

	   In order for tone queue to recognize that a 'low tone
	   queue' callback needs to be called, the level in tq needs
	   to drop from 2 to 1.

	   Almost every queued character guarantees that there will be
	   at least two tones, e.g for 'E' it is dash + following
	   space. But what about a ' ' character?

	   If we play ' ' character as single tone, there is only one
	   tone in tone queue, and the tone queue manager can't
	   recognize when the level drops from 2 to 1 (and thus the
	   'low level' callback won't be called).

	   If we play ' ' character as two separate tones (as we do
	   this in this function), the tone queue manager can
	   recognize level dropping from 2 to 1. Then the passing of
	   critical level can be noticed, and "low level" callback can
	   be called. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, 0, gen->eow_space_len, CW_SLOPE_MODE_NO_SLOPES);
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

	if (rv == CW_SUCCESS) {
		CW_TONE_INIT(&tone, 0, gen->adjustment_space_len, CW_SLOPE_MODE_NO_SLOPES);
		rv = cw_tq_enqueue_internal(gen->tq, &tone);
	}

	return rv;
}





/**
   \brief Play the given representation

   Function plays given \p representation using given \p
   generator. Every mark from the \p representation is followed by a
   standard inter-mark space.

   If \p partial is false, the representation is treated as a complete
   (non-partial) data, and a standard end-of-character space is played
   at the end (in addition to last inter-mark space). Total length of
   space at the end (inter-mark space + end-of-character space) is ~3
   Units.

   If \p partial is true, the standard end-of-character space is not
   appended. However, the standard inter-mark space is played at the
   end.

   Function sets errno to EAGAIN if there is not enough space in tone
   queue to enqueue \p representation.

   Function validates \p representation using
   cw_representation_is_valid().  Function sets errno to EINVAL if \p
   representation is not valid.

   \param gen - generator used to play the representation
   \param representation - representation to play
   \param partial

   \return CW_FAILURE on failure
   \return CW_SUCCESS on success
*/
int cw_gen_play_representation_internal(cw_gen_t *gen, const char *representation, bool partial)
{
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Before we let this representation loose on tone generation,
	   we'd really like to know that all of its tones will get queued
	   up successfully.  The right way to do this is to calculate the
	   number of tones in our representation, then check that the space
	   exists in the tone queue. However, since the queue is comfortably
	   long, we can get away with just looking for a high water mark.  */
	if (cw_tq_length_internal(gen->tq) >= gen->tq->high_water_mark) {
		errno = EAGAIN;
		return CW_FAILURE;
	}

	/* Play the marks. Every mark is followed by end-of-mark
	   space. */
	for (int i = 0; representation[i] != '\0'; i++) {
		if (!cw_gen_play_mark_internal(gen, representation[i])) {
			return CW_FAILURE;
		}
	}

	/* Check if we should append end-of-character space at the end
	   (in addition to last end-of-mark space). */
	if (!partial) {
		if (!cw_gen_play_eoc_space_internal(gen)) {
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





/**
   \brief Look up and play a given ASCII character as Morse code

   If \p partial is set, the end-of-character space is not appended
   after last mark of Morse code.

   Function sets errno to ENOENT if \p character is not a recognized character.

   \param gen - generator to be used to play character
   \param character - character to play
   \param partial

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_valid_character_internal(cw_gen_t *gen, char character, int partial)
{
	if (!gen) {
		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
			      "libcw: no generator available");
		return CW_FAILURE;
	}

	/* ' ' character (i.e. end-of-word space) is a special case. */
	if (character == ' ') {
		return cw_gen_play_eow_space_internal(gen);
	}

	/* Lookup the character, and play it. */
	const char *representation = cw_character_to_representation_internal(character);
	if (!representation) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	if (!cw_gen_play_representation_internal(gen, representation, partial)) {
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





/**
   \brief Look up and play a given ASCII character as Morse

   The end of character delay is appended to the Morse sent.

   On success the function returns CW_SUCCESS.
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

   \param gen - generator to play with
   \param c - character to play

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_character_internal(cw_gen_t *gen, char c)
{
	/* The call to _is_valid() is placed outside of
	   cw_gen_play_valid_character_internal() for performance
	   reasons.

	   Or to put it another way:
	   cw_gen_play_valid_character_internal() was created to be
	   called in loop for all characters of validated string, so
	   there was no point in validating all characters separately
	   in that function. */

	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_gen_play_valid_character_internal(gen, c, false);
	}
}





/**
   \brief Look up and play a given ASCII character as Morse code

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

   \param gen - generator to use
   \param c - character to play

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_character_parital_internal(cw_gen_t *gen, char c)
{
	/* The call to _is_valid() is placed outside of
	   cw_gen_play_valid_character_internal() for performance
	   reasons.

	   Or to put it another way:
	   cw_gen_play_valid_character_internal() was created to be
	   called in loop for all characters of validated string, so
	   there was no point in validating all characters separately
	   in that function. */

	if (!cw_character_is_valid(c)) {
		errno = ENOENT;
		return CW_FAILURE;
	} else {
		return cw_gen_play_valid_character_internal(gen, c, true);
	}
}





/**
   \brief Play a given ASCII string in Morse code

   errno is set to ENOENT if any character in the string is not a
   valid Morse character.

   errno is set to EBUSY if audio sink or keying system is busy.

   errno is set to EAGAIN if the tone queue is full or if the tone
   queue runs out of space part way through queueing the string.
   However, an indeterminate number of the characters from the string
   will have already been queued.

   For safety, clients can ensure the tone queue is empty before
   queueing a string, or use cw_gen_play_character_internal() if they
   need finer control.

   This routine queues its arguments for background processing, the
   actual sending happens in background processing. See
   cw_wait_for_tone() and cw_wait_for_tone_queue() for ways to check
   the progress of sending.

   \param gen - generator to use
   \param string - string to play

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_play_string_internal(cw_gen_t *gen, const char *string)
{
	/* Check the string is composed of sendable characters. */
	if (!cw_string_is_valid(string)) {
		errno = ENOENT;
		return CW_FAILURE;
	}

	/* Send every character in the string. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!cw_gen_play_valid_character_internal(gen, string[i], false))
			return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
  \brief Reset essential sending parameters to their initial values

  \param gen
*/
void cw_gen_reset_send_parameters_internal(cw_gen_t *gen)
{
	cw_assert (gen, "generator is NULL");

	gen->send_speed = CW_SPEED_INITIAL;
	gen->frequency = CW_FREQUENCY_INITIAL;
	gen->volume_percent = CW_VOLUME_INITIAL;
	gen->volume_abs = (gen->volume_percent * CW_AUDIO_VOLUME_RANGE) / 100;
	gen->gap = CW_GAP_INITIAL;
	gen->weighting = CW_WEIGHTING_INITIAL;

	gen->parameters_in_sync = false;

	return;

}





/**
   \brief Synchronize generator's low level timing parameters

   \param gen - generator
*/
void cw_gen_sync_parameters_internal(cw_gen_t *gen)
{
	cw_assert (gen, "generator is NULL");

	/* Do nothing if we are already synchronized. */
	if (gen->parameters_in_sync) {
		return;
	}

	/* Set the length of a Dot to be a Unit with any weighting
	   adjustment, and the length of a Dash as three Dot lengths.
	   The weighting adjustment is by adding or subtracting a
	   length based on 50 % as a neutral weighting. */
	int unit_length = CW_DOT_CALIBRATION / gen->send_speed;
	int weighting_length = (2 * (gen->weighting - 50) * unit_length) / 100;
	gen->dot_len = unit_length + weighting_length;
	gen->dash_len = 3 * gen->dot_len;

	/* End-of-mark space length is one Unit, perhaps adjusted.
	   End-of-character space length is three Units total.
	   End-of-word space length is seven Units total.

	   WARNING: notice how the eoc and eow spaces are
	   calculated. They aren't full 3 units and 7 units. They are
	   2 units (which takes into account preceding eom space
	   length), and 5 units (which takes into account preceding
	   eom *and* eoc space length). So these two lengths are
	   *additional* ones, i.e. in addition to (already existing)
	   eom and/or eoc space.  Whether this is good or bad idea to
	   calculate them like this is a separate topic. Just be aware
	   of this fact.

	   The end-of-mark length is adjusted by 28/22 times
	   weighting length to keep PARIS calibration correctly
	   timed (PARIS has 22 full units, and 28 empty ones).
	   End-of-mark and end of character delays take
	   weightings into account. */
	gen->eom_space_len = unit_length - (28 * weighting_length) / 22;  /* End-of-mark space, a.k.a. regular inter-mark space. */
	gen->eoc_space_len = 3 * unit_length - gen->eom_space_len;
	gen->eow_space_len = 7 * unit_length - gen->eoc_space_len;
	gen->additional_space_len = gen->gap * unit_length;

	/* For "Farnsworth", there also needs to be an adjustment
	   delay added to the end of words, otherwise the rhythm is
	   lost on word end.
	   I don't know if there is an "official" value for this,
	   but 2.33 or so times the gap is the correctly scaled
	   value, and seems to sound okay.

	   Thanks to Michael D. Ivey <ivey@gweezlebur.com> for
	   identifying this in earlier versions of libcw. */
	gen->adjustment_space_len = (7 * gen->additional_space_len) / 3;

	cw_debug_msg ((&cw_debug_object), CW_DEBUG_PARAMETERS, CW_DEBUG_INFO,
		      "libcw: send usec timings <%d [wpm]>: dot: %d, dash: %d, %d, %d, %d, %d, %d",
		      gen->send_speed, gen->dot_len, gen->dash_len,
		      gen->eom_space_len, gen->eoc_space_len,
		      gen->eow_space_len, gen->additional_space_len, gen->adjustment_space_len);

	/* Generator parameters are now in sync. */
	gen->parameters_in_sync = true;

	return;
}





/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   'key' is a verb here. The function should be called only on "key
   down" (begin mark) event from hardware straight key.

   The function is called in very specific context, see cw_key module
   for details.

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_key_begin_mark_internal(cw_gen_t *gen)
{
	/* In case of straight key we don't know at all how long the
	   tone should be (we don't know for how long the key will be
	   closed).

	   Let's enqueue a beginning of mark (rising slope) +
	   "forever" (constant) tone. The constant tone will be played
	   until function receives CW_KEY_STATE_OPEN key state. */

	cw_tone_t tone;
	CW_TONE_INIT(&tone, gen->frequency, gen->tone_slope.len, CW_SLOPE_MODE_RISING_SLOPE);
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

	if (rv == CW_SUCCESS) {

		CW_TONE_INIT(&tone, gen->frequency, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		tone.forever = true;
		rv = cw_tq_enqueue_internal(gen->tq, &tone);

		cw_debug_msg ((&cw_debug_object_dev), CW_DEBUG_TONE_QUEUE, CW_DEBUG_DEBUG,
			      "libcw: tone queue: len = %"PRIu32"", cw_tq_length_internal(gen->tq));
	}

	return rv;
}





/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   'key' is a verb here. The function should be called only on "key
   up" (begin space) event from hardware straight key.

   The function is called in very specific context, see cw_key module
   for details.

   \param gen - generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_key_begin_space_internal(cw_gen_t *gen)
{
	if (gen->audio_system == CW_AUDIO_CONSOLE) {
		/* Play just a bit of silence, just to switch
		   buzzer from playing a sound to being silent. */
		cw_tone_t tone;
		CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
		return cw_tq_enqueue_internal(gen->tq, &tone);
	} else {
		/* For soundcards a falling slope with volume from max
		   to zero should be enough, but... */
		cw_tone_t tone;
		CW_TONE_INIT(&tone, gen->frequency, gen->tone_slope.len, CW_SLOPE_MODE_FALLING_SLOPE);
		int rv = cw_tq_enqueue_internal(gen->tq, &tone);

		if (rv == CW_SUCCESS) {
			/* ... but on some occasions, on some
			   platforms, some sound systems may need to
			   constantly play "silent" tone. These four
			   lines of code are just for them.

			   It would be better to avoid queueing silent
			   "forever" tone because this increases CPU
			   usage. It would be better to simply not to
			   queue any new tones after "falling slope"
			   tone. Silence after the last falling slope
			   would simply last on itself until there is
			   new tone on queue to play. */
			CW_TONE_INIT(&tone, 0, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
			tone.forever = true;
			rv = cw_tq_enqueue_internal(gen->tq, &tone);
		}

		return rv;
	}
}





/**
   Helper function intended to hide details of tone queue and of
   enqueueing a tone from cw_key module.

   'key' is a verb here. It indicates, that the function should be
   called on hardware key events only. Since we enqueue symbols, we
   know that they have limited, specified length. This means that the
   function should be called for events from iambic keyer.

   'Pure' means without any end-of-mark spaces.

   The function is called in very specific context, see cw_key module
   for details.

   \param gen - generator
   \param symbol - symbol to enqueue (Space/Dot/Dash)

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_gen_key_pure_symbol_internal(cw_gen_t *gen, char symbol)
{
	cw_tone_t tone;

	if (symbol == CW_DOT_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dot_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_DASH_REPRESENTATION) {
		CW_TONE_INIT(&tone, gen->frequency, gen->dash_len, CW_SLOPE_MODE_STANDARD_SLOPES);

	} else if (symbol == CW_SYMBOL_SPACE) {
		CW_TONE_INIT(&tone, 0, gen->eom_space_len, CW_SLOPE_MODE_NO_SLOPES);

	} else {
		cw_assert (0, "unknown key symbol '%d'", symbol);
	}

	return cw_tq_enqueue_internal(gen->tq, &tone);
}





/* *** Unit tests *** */





#ifdef LIBCW_UNIT_TESTS


#include "libcw_test.h"





/**
   tests::cw_gen_new_internal()
   tests::cw_gen_delete_internal()
*/
unsigned int test_cw_gen_new_delete_internal(void)
{
	int p = fprintf(stdout, "libcw/gen: cw_gen_new/start/stop/delete_internal():\n");
	fflush(stdout);

	/* Arbitrary number of calls to a set of tested functions. */
	int n = 100;

	/* new() + delete() */
	fprintf(stderr, "libcw/gen: generator test 1/4\n");
	for (int i = 0; i < n; i++) {


		cw_gen_t *gen = cw_gen_new_internal(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		/* Try to access some fields in cw_gen_t just to be
		   sure that the gen has been allocated properly. */
		cw_assert (gen->buffer_sub_start == 0, "buffer_sub_start in new generator is not at zero");
		gen->buffer_sub_stop = gen->buffer_sub_start + 10;
		cw_assert (gen->buffer_sub_stop == 10, "buffer_sub_stop didn't store correct new value");

		cw_assert (gen->client.name == (char *) NULL, "initial value of generator's client name is not NULL");

		cw_assert (gen->tq, "tone queue is NULL");

		cw_gen_delete_internal(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	n = 5;


	/* new() + start() + delete() (skipping stop() on purpose). */
	for (int i = 0; i < n; i++) {
		fprintf(stderr, "libcw/gen: generator test 2/4, loop #%d/%d\n", i, n);

		cw_gen_t *gen = cw_gen_new_internal(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		int rv = cw_gen_start_internal(gen);
		cw_assert (rv, "failed to start generator (loop #%d)", i);

		cw_gen_delete_internal(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	/* new() + stop() + delete() (skipping start() on purpose). */
	fprintf(stderr, "libcw/gen: generator test 3/4\n");
	for (int i = 0; i < n; i++) {
		cw_gen_t *gen = cw_gen_new_internal(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		int rv = cw_gen_stop_internal(gen);
		cw_assert (rv, "failed to stop generator (loop #%d)", i);

		cw_gen_delete_internal(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}



	/* Inner loop. */
	int m = n;


	/* new() + start() + stop() + delete() */
	for (int i = 0; i < n; i++) {
		fprintf(stderr, "libcw/gen: generator test 4/4, loop #%d/%d\n", i, n);

		cw_gen_t *gen = cw_gen_new_internal(CW_AUDIO_NULL, NULL);
		cw_assert (gen, "failed to initialize generator (loop #%d)", i);

		for (int j = 0; j < m; j++) {
			int rv = cw_gen_start_internal(gen);
			cw_assert (rv, "failed to start generator (loop #%d-%d)", i, j);

			rv = cw_gen_stop_internal(gen);
			cw_assert (rv, "failed to stop generator (loop #%d-%d)", i, j);
		}

		cw_gen_delete_internal(&gen);
		cw_assert (gen == NULL, "delete() didn't set the pointer to NULL (loop #%d)", i);
	}


	p = fprintf(stdout, "libcw/gen: cw_gen_new/start/stop/delete_internal():");
	CW_TEST_PRINT_TEST_RESULT(false, p);
	fflush(stdout);

	return 0;
}




unsigned int test_cw_generator_set_tone_slope(void)
{
	int p = fprintf(stdout, "libcw/gen: cw_generator_set_tone_slope():");

	int audio_system = CW_AUDIO_NULL;

	/* Test 0: test property of newly created generator. */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test 0");


		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == CW_AUDIO_SLOPE_LEN,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		cw_gen_delete_internal(&gen);
	}



	/* Test A: pass conflicting arguments.

	   "A: If you pass to function conflicting values of \p
	   slope_shape and \p slope_len, the function will return
	   CW_FAILURE. These conflicting values are rectangular slope
	   shape and larger than zero slope length. You just can't
	   have rectangular slopes that have non-zero length." */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test A");


		int rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 10);
		cw_assert (!rv, "function accepted conflicting arguments");


		cw_gen_delete_internal(&gen);
	}



	/* Test B: pass '-1' as both arguments.

	   "B: If you pass to function '-1' as value of both \p
	   slope_shape and \p slope_len, the function won't change
	   any of the related two generator's parameters." */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test B");


		int shape_before = gen->tone_slope.shape;
		int len_before = gen->tone_slope.len;

		int rv = cw_generator_set_tone_slope(gen, -1, -1);
		cw_assert (rv, "failed to set tone slope");

		cw_assert (gen->tone_slope.shape == shape_before,
			   "tone slope shape changed from %d to %d", shape_before, gen->tone_slope.shape);

		cw_assert (gen->tone_slope.len == len_before,
			   "tone slope length changed from %d to %d", len_before, gen->tone_slope.len);


		cw_gen_delete_internal(&gen);
	}



	/* Test C1

	   "C1: If you pass to function '-1' as value of either \p
	   slope_shape or \p slope_len, the function will attempt to
	   set only this generator's parameter that is different than
	   '-1'." */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test C1");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_LINEAR;
		int rv = cw_generator_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set linear slope shape with unchanged slope length");

		/* At this point only slope shape should be updated. */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to preserve slope length; length is %d", gen->tone_slope.len);


		/* Set only new slope length. */
		expected_len = 30;
		rv = cw_generator_set_tone_slope(gen, -1, expected_len);
		cw_assert (rv, "failed to set positive slope length with unchanged slope shape");

		/* At this point only slope length should be updated
		   (compared to previous function call). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to preserve shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to set new slope length; length is %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_SINE;
		rv = cw_generator_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set new slope shape with unchanged slope length");

		/* At this point only slope shape should be updated
		   (compared to previous function call). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to preserve slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete_internal(&gen);
	}



	/* Test C2

	   "C2: However, if selected slope shape is rectangular,
	   function will set generator's slope length to zero, even if
	   value of \p slope_len is '-1'." */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test C2");


		/* At the beginning of test these values are
		   generator's initial values.  As test progresses,
		   some other values will be expected after successful
		   calls to tested function. */
		int expected_shape = CW_TONE_SLOPE_SHAPE_RAISED_COSINE;
		int expected_len = CW_AUDIO_SLOPE_LEN;


		/* At this point generator should have initial values
		   of its parameters (yes, that's test zero again). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "new generator has unexpected initial slope shape %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "new generator has unexpected initial slope length %d", gen->tone_slope.len);


		/* Set only new slope shape. */
		expected_shape = CW_TONE_SLOPE_SHAPE_RECTANGULAR;
		expected_len = 0; /* Even though we won't pass this to function, this is what we expect to get after this call. */
		int rv = cw_generator_set_tone_slope(gen, expected_shape, -1);
		cw_assert (rv, "failed to set rectangular slope shape with unchanged slope length");

		/* At this point slope shape AND slope length should
		   be updated (slope length is updated only because of
		   requested rectangular slope shape). */
		cw_assert (gen->tone_slope.shape == expected_shape,
			   "failed to set new shape of slope; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == expected_len,
			   "failed to get expected slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete_internal(&gen);
	}



	/* Test D

	   "D: Notice that the function allows non-rectangular slope
	   shape with zero length of the slopes. The slopes will be
	   non-rectangular, but just unusually short." */
	{
		cw_gen_t *gen = cw_gen_new_internal(audio_system, NULL);
		cw_assert (gen, "failed to initialize generator in test D");


		int rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_LINEAR, 0);
		cw_assert (rv, "failed to set linear slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_LINEAR,
			   "failed to set linear slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RAISED_COSINE, 0);
		cw_assert (rv, "failed to set raised cosine slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RAISED_COSINE,
			   "failed to set raised cosine slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_SINE, 0);
		cw_assert (rv, "failed to set sine slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_SINE,
			   "failed to set sine slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		rv = cw_generator_set_tone_slope(gen, CW_TONE_SLOPE_SHAPE_RECTANGULAR, 0);
		cw_assert (rv, "failed to set rectangular slope with zero length");
		cw_assert (gen->tone_slope.shape == CW_TONE_SLOPE_SHAPE_RECTANGULAR,
			   "failed to set rectangular slope shape; shape is %d", gen->tone_slope.shape);
		cw_assert (gen->tone_slope.len == 0,
			   "failed to set zero slope length; length is %d", gen->tone_slope.len);


		cw_gen_delete_internal(&gen);
	}


	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Test some assertions about CW_TONE_SLOPE_SHAPE_*

   Code in this file depends on the fact that these values are
   different than -1. I think that ensuring that they are in general
   small, non-negative values is a good idea.

   I'm testing these values to be sure that when I get a silly idea to
   modify them, the test will catch this modification.
*/
unsigned int test_cw_gen_tone_slope_shape_enums(void)
{
	int p = fprintf(stdout, "libcw/gen: CW_TONE_SLOPE_SHAPE_*:");

	cw_assert (CW_TONE_SLOPE_SHAPE_LINEAR >= 0,        "CW_TONE_SLOPE_SHAPE_LINEAR is negative: %d",        CW_TONE_SLOPE_SHAPE_LINEAR);
	cw_assert (CW_TONE_SLOPE_SHAPE_RAISED_COSINE >= 0, "CW_TONE_SLOPE_SHAPE_RAISED_COSINE is negative: %d", CW_TONE_SLOPE_SHAPE_RAISED_COSINE);
	cw_assert (CW_TONE_SLOPE_SHAPE_SINE >= 0,          "CW_TONE_SLOPE_SHAPE_SINE is negative: %d",          CW_TONE_SLOPE_SHAPE_SINE);
	cw_assert (CW_TONE_SLOPE_SHAPE_RECTANGULAR >= 0,   "CW_TONE_SLOPE_SHAPE_RECTANGULAR is negative: %d",   CW_TONE_SLOPE_SHAPE_RECTANGULAR);

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/* Version of test_cw_gen_forever() to be used in libcw_test_internal
   test executable.

   It's not a test of a "forever" function, but of "forever"
   functionality.
*/
unsigned int test_cw_gen_forever_internal(void)
{
	int seconds = 2;
	int p = fprintf(stdout, "libcw/gen: forever tone (%d seconds):", seconds);
	fflush(stdout);

	unsigned int rv = test_cw_gen_forever_sub(2, CW_AUDIO_NULL, (const char *) NULL);
	cw_assert (rv == 0, "\"forever\" test failed");

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





unsigned int test_cw_gen_forever_sub(int seconds, int audio_system, const char *audio_device)
{
	cw_gen_t *gen = cw_gen_new_internal(audio_system, audio_device);
	cw_assert (gen, "ERROR: failed to create generator\n");
	cw_gen_start_internal(gen);
	sleep(1);

	cw_tone_t tone;
	/* Just some acceptable values. */
	int len = 100; /* [us] */
	int freq = 500;

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_RISING_SLOPE);
	cw_tq_enqueue_internal(gen->tq, &tone);

	CW_TONE_INIT(&tone, freq, gen->quantum_len, CW_SLOPE_MODE_NO_SLOPES);
	tone.forever = true;
	int rv = cw_tq_enqueue_internal(gen->tq, &tone);

#ifdef __FreeBSD__  /* Tested on FreeBSD 10. */
	/* Separate path for FreeBSD because for some reason signals
	   badly interfere with value returned through second arg to
	   nanolseep().  Try to run the section in #else under FreeBSD
	   to see what happens - value returned by nanosleep() through
	   "rem" will be increasing. */
	fprintf(stderr, "enter any character to end \"forever\" tone\n");
	char c;
	scanf("%c", &c);
#else
	struct timespec t;
	cw_usecs_to_timespec_internal(&t, seconds * CW_USECS_PER_SEC);
	cw_nanosleep_internal(&t);
#endif

	CW_TONE_INIT(&tone, freq, len, CW_SLOPE_MODE_FALLING_SLOPE);
	rv = cw_tq_enqueue_internal(gen->tq, &tone);
	cw_assert (rv, "failed to enqueue last tone");

	cw_gen_delete_internal(&gen);

	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
