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

#ifndef _LIBCW_H
#define _LIBCW_H

#include <sys/time.h>  /* For struct timeval */

#include <stdint.h>    /* int16_t */
#include <pthread.h>
#include <stdbool.h>


static const int        CW_AUDIO_CHANNELS = 1;                /* Sound in mono */


#if defined(__cplusplus)
extern "C"
{
#endif


enum cw_return_values {
	CW_FAILURE = false,
	CW_SUCCESS = true };

/* supported audio sound systems */
enum cw_audio_systems {
	CW_AUDIO_NONE = 0,  /* initial value; this is not the same as CW_AUDIO_NULL */
	CW_AUDIO_NULL,      /* empty audio output (no sound, just timing); this is not the same as CW_AUDIO_NONE */
	CW_AUDIO_CONSOLE,   /* console buzzer */
	CW_AUDIO_OSS,
	CW_AUDIO_ALSA,
	CW_AUDIO_PA,        /* PulseAudio */
	CW_AUDIO_SOUNDCARD  /* OSS, ALSA or PulseAudio (PA) */
};

enum {
	CW_KEY_STATE_OPEN = 0,  /* key is open, no electrical contact in key, no sound */
	CW_KEY_STATE_CLOSED     /* key is closed, there is an electrical contact in key, a sound is generated */
};


typedef int16_t cw_sample_t;



/* Default outputs for audio systems. Used by libcw unless
   client code decides otherwise. */
#define CW_DEFAULT_NULL_DEVICE      ""
#define CW_DEFAULT_CONSOLE_DEVICE   "/dev/console"
#define CW_DEFAULT_OSS_DEVICE       "/dev/audio"
#define CW_DEFAULT_ALSA_DEVICE      "default"
#define CW_DEFAULT_PA_DEVICE        "( default )"


/* Limits on values of CW send and timing parameters */
#define CW_SPEED_MIN             4   /* Lowest WPM allowed */
#define CW_SPEED_MAX            60   /* Highest WPM allowed */
#define CW_SPEED_STEP            1
#define CW_SPEED_INITIAL        12   /* Initial send speed in WPM */
#define CW_FREQUENCY_MIN         0   /* Lowest tone allowed (0=silent) */
#define CW_FREQUENCY_MAX      4000   /* Highest tone allowed */
#define CW_FREQUENCY_INITIAL   800   /* Initial tone in Hz */
#define CW_FREQUENCY_STEP       20
#define CW_VOLUME_MIN            0   /* Quietest volume allowed (0=silent) */
#define CW_VOLUME_MAX          100   /* Loudest volume allowed */
#define CW_VOLUME_INITIAL       70   /* Initial volume percent */
#define CW_VOLUME_STEP           1
#define CW_GAP_MIN               0   /* Lowest extra gap allowed */
#define CW_GAP_MAX              60   /* Highest extra gap allowed */
#define CW_GAP_INITIAL           0   /* Initial gap setting */
#define CW_GAP_STEP              1
#define CW_WEIGHTING_MIN        20   /* Lowest weighting allowed */
#define CW_WEIGHTING_MAX        80   /* Highest weighting allowed */
#define CW_WEIGHTING_INITIAL    50   /* Initial weighting setting */
#define CW_TOLERANCE_MIN         0   /* Lowest receive tolerance allowed */
#define CW_TOLERANCE_MAX        90   /* Highest receive tolerance allowed */
#define CW_TOLERANCE_INITIAL    50   /* Initial tolerance setting */


/*
 * Representation characters for Dot and Dash.  Only the following
 * characters are permitted in Morse representation strings.
 */
enum { CW_DOT_REPRESENTATION = '.', CW_DASH_REPRESENTATION = '-' };



/* Debugging support. */

/* Definitions of debug flags. */
enum {
	/* Suppress KIOCSOUND ioctls.
	   The flag is unused at the moment in libcw.c. */
	CW_DEBUG_SILENT               = 1 << 0,

	/* Print out keying control data (key open / key closed). */
	CW_DEBUG_KEYING               = 1 << 1,

	/* Print out information related to main object generating
	   audio (i.e. the generator). */
	CW_DEBUG_GENERATOR            = 1 << 2,

	/* Print out tone queue data. */
	CW_DEBUG_TONE_QUEUE           = 1 << 3,

	/* Print out send and receive timing parameters. */
	CW_DEBUG_PARAMETERS           = 1 << 4,

	/* Print out changes of 'receive state'. */
	CW_DEBUG_RECEIVE_STATES       = 1 << 5,

	/* Print out iambic keyer information. */
	CW_DEBUG_KEYER_STATES         = 1 << 6,

	/* Print out straight key information. */
	CW_DEBUG_STRAIGHT_KEY_STATES  = 1 << 7,

	/* Print out table lookup results. */
	CW_DEBUG_LOOKUPS              = 1 << 8,

	/* Print out finalization actions. */
	CW_DEBUG_FINALIZATION         = 1 << 9,

	/* Print out information related to calls to standard library
	   functions.  This does not include calls that are directly
	   related to configuring audio devices (e.g. calling open()
	   to open OSS device).

	   Printing debug information about problems with malloc()
	   seems to be most common use case for this flag. */
	CW_DEBUG_STDLIB               = 1 << 10,

	/* Print out any events directly related to sound systems: to
	   opening audio device, configuring it, writing to it, and
	   closing it.  In case of e.g. OSS sound system, this may
	   also include information about libc's open(), write() and
	   ioctl() calls, as they are directly related to
	   opening/configuring/writing to audio device. */
	CW_DEBUG_SOUND_SYSTEM         = 1 << 11,

	/* Print out information related to internal states / errors /
	   inconsistencies of libcw library. */
	CW_DEBUG_INTERNAL             = 1 << 12,

	/* Bit mask of all defined debug bits. */
	CW_DEBUG_MASK                 = (1 << 13) - 1
};



/* Debug levels of debug messages. */
enum {
	CW_DEBUG_DEBUG   = 0,
	CW_DEBUG_INFO    = 1,
	CW_DEBUG_WARNING = 2,
	CW_DEBUG_ERROR   = 3,
	CW_DEBUG_NONE    = 4  /* Don't print any debug info. */
};




/* Values deciding the shape of slopes of tones generated by
   generator.

   If a generated tone is declared to have one or two slopes,
   generator has to know what shape of the slope(s) should be. Since
   the shape of tones is common for all tones generated by generator,
   shape of tone is a property of generator rather than of tone.

   These names are to be used as values of argument 'slope_shape' of
   cw_generator_set_tone_slope() function.  */
enum {
	CW_TONE_SLOPE_SHAPE_LINEAR,          /* Ramp/linearly raising slope. */
	CW_TONE_SLOPE_SHAPE_RAISED_COSINE,   /* Shape of cosine function in range <-pi - zero).  */
	CW_TONE_SLOPE_SHAPE_SINE,            /* Shape of sine function in range <zero - pi/2). */
	CW_TONE_SLOPE_SHAPE_RECTANGULAR      /* Slope changes from zero for sample n, to full amplitude of tone in sample n+1. */
};

typedef struct cw_gen_struct cw_gen_t;


/* Functions handling library meta data */
extern int  cw_version(void);
extern void cw_license(void);
extern const char *cw_get_audio_system_label(int audio_system);



/* Functions handling 'generator' */
extern int  cw_generator_new(int audio_system, const char *device);
extern void cw_generator_delete(void);
extern int  cw_generator_start(void);
extern void cw_generator_stop(void);
extern const char *cw_generator_get_audio_system_label(void);
/* FIXME: first argument of the function is gen, but no function provides access to generator variable. */
extern int  cw_generator_set_tone_slope(cw_gen_t *gen, int slope_shape, int slope_usecs);

/* Core Morse code data and lookup */
extern int   cw_get_character_count(void);
extern void  cw_list_characters(char *list);
extern int   cw_get_maximum_representation_length(void);
extern char *cw_character_to_representation(int c);
extern bool  cw_representation_is_valid(const char *representation);
extern int   cw_representation_to_character(const char *representation);


/* Extended Morse code data and lookup (procedural signals) */
extern int cw_get_procedural_character_count(void);
extern void cw_list_procedural_characters(char *list);
extern int cw_get_maximum_procedural_expansion_length(void);

extern int cw_lookup_procedural_character (char c, char *representation,
                                           int *is_usually_expanded);


/* Phonetic alphabet */
extern int cw_get_maximum_phonetic_length(void);
extern int cw_lookup_phonetic(char c, char *phonetic);


/* Morse code controls and timing parameters */
extern void cw_get_speed_limits(int *min_speed, int *max_speed);
extern void cw_get_frequency_limits(int *min_frequency, int *max_frequency);
extern void cw_get_volume_limits(int *min_volume, int *max_volume);
extern void cw_get_gap_limits(int *min_gap, int *max_gap);
extern void cw_get_tolerance_limits(int *min_tolerance, int *max_tolerance);
extern void cw_get_weighting_limits(int *min_weighting, int *max_weighting);
extern void cw_reset_send_receive_parameters(void);
extern int cw_set_send_speed(int new_value);
extern int cw_set_receive_speed(int new_value);
extern int cw_set_frequency(int new_value);
extern int cw_set_volume(int new_value);
extern int cw_set_gap(int new_value);
extern int cw_set_tolerance(int new_value);
extern int cw_set_weighting(int new_value);
extern int cw_get_send_speed(void);
extern int cw_get_receive_speed(void);
extern int cw_get_frequency(void);
extern int cw_get_volume(void);
extern int cw_get_gap(void);
extern int cw_get_tolerance(void);
extern int cw_get_weighting(void);
extern void cw_get_send_parameters(int *dot_usecs, int *dash_usecs,
				   int *end_of_element_usecs,
				   int *end_of_character_usecs,
				   int *end_of_word_usecs,
				   int *additional_usecs,
				   int *adjustment_usecs);
extern void cw_get_receive_parameters(int *dot_usecs, int *dash_usecs,
				      int *dot_min_usecs, int *dot_max_usecs,
				      int *dash_min_usecs, int *dash_max_usecs,
				      int *end_of_element_min_usecs,
				      int *end_of_element_max_usecs,
				      int *end_of_element_ideal_usecs,
				      int *end_of_character_min_usecs,
				      int *end_of_character_max_usecs,
				      int *end_of_character_ideal_usecs,
				      int *adaptive_threshold);
extern int cw_set_noise_spike_threshold(int new_value);
extern int cw_get_noise_spike_threshold(void);


extern void cw_block_callback(int block);


/* General control of console buzzer and of soundcard */
extern const char *cw_get_console_device(void);
extern const char *cw_get_soundcard_device(void);


extern bool cw_is_null_possible(const char *device);
extern bool cw_is_console_possible(const char *device);
extern bool cw_is_oss_possible(const char *device);
extern bool cw_is_alsa_possible(const char *device);
extern bool cw_is_pa_possible(const char *device);




/* Finalization and cleanup */
extern void cw_complete_reset(void);
extern int  cw_register_signal_handler(int signal_number,
				       void (*callback_func)(int));
extern int  cw_unregister_signal_handler(int signal_number);




/* Keying control */
extern void cw_register_keying_callback(void (*callback_func)(void*, int),
					void *callback_arg);
extern void cw_iambic_keyer_register_timer(struct timeval *timer);


/* Tone queue */
extern int cw_register_tone_queue_low_callback(void (*callback_func) (void*),
                                                void *callback_arg, int level);
extern bool cw_is_tone_busy(void);
extern int  cw_wait_for_tone(void);
extern int  cw_wait_for_tone_queue(void);
extern int  cw_wait_for_tone_queue_critical(int level);
extern bool cw_is_tone_queue_full(void);
extern int  cw_get_tone_queue_capacity(void);
extern int  cw_get_tone_queue_length(void);
extern void cw_flush_tone_queue(void);
extern int  cw_queue_tone(int usecs, int frequency);
extern void cw_reset_tone_queue(void);



/* Sending */
extern int cw_send_dot(void);
extern int cw_send_dash(void);
extern int cw_send_character_space(void);
extern int cw_send_word_space(void);
extern int cw_send_representation(const char *representation);
extern int cw_send_representation_partial(const char *representation);
extern int cw_send_character(char c);
extern int cw_send_character_partial(char c);
extern int cw_send_string(const char *string);

extern bool cw_character_is_valid(char c);
extern bool cw_string_is_valid(const char *string);



/* Receive tracking and statistics helpers */
extern void cw_get_receive_statistics(double *dot_sd, double *dash_sd,
                                       double *element_end_sd,
                                       double *character_end_sd);
extern void cw_reset_receive_statistics(void);


/* Receiving */
extern void cw_enable_adaptive_receive(void);
extern void cw_disable_adaptive_receive(void);
extern bool cw_get_adaptive_receive_state(void);
extern int cw_start_receive_tone(const struct timeval *timestamp);
extern int cw_end_receive_tone(const struct timeval *timestamp);

/* If I'm reading xcwcp/receiver.cc correctly then currently libcw
   implements polling interface to receiver code. Client code must
   periodically poll libcw to see if there is a new character or space
   in receiver's representation buffer. These four functions below are
   the main part of "polling".

   TODO: implement alternative way of accessing receiver's
   representation buffer (or maybe sharing the buffer's content by
   receiver). Some kind of scheme where receiver informs client code
   about the fact that the receiver ended receiving marks/spaces and
   recognized full character or space. Some kind of notification to
   the client code, or calling client's callback function. */
extern int cw_receive_buffer_dot(const struct timeval *timestamp);
extern int cw_receive_buffer_dash(const struct timeval *timestamp);
extern int cw_receive_representation(const struct timeval *timestamp,
				     char *representation,
				     bool *is_end_of_word, bool *is_error);
extern int cw_receive_character(const struct timeval *timestamp,
                                 char *c, bool *is_end_of_word, bool *is_error);
/* It seems that this function is a part of the polling interface as
   well. It is called by client code in xcwcp/receiver.cc after
   passing recognized character from receiver to client code. It makes
   space in the receiver's buffer for next character. */
extern void cw_clear_receive_buffer(void);

extern int cw_get_receive_buffer_capacity(void);
extern int cw_get_receive_buffer_length(void);
extern void cw_reset_receive(void);


/* Iambic keyer */
extern void cw_enable_iambic_curtis_mode_b(void);
extern void cw_disable_iambic_curtis_mode_b(void);
extern int cw_get_iambic_curtis_mode_b_state(void);

extern int cw_notify_keyer_paddle_event(int dot_paddle_state,
					int dash_paddle_state);
extern int cw_notify_keyer_dot_paddle_event(int dot_paddle_state);
extern int cw_notify_keyer_dash_paddle_event(int dash_paddle_state);
extern void cw_get_keyer_paddles(int *dot_paddle_state,
				 int *dash_paddle_state);
extern void cw_get_keyer_paddle_latches(int *dot_paddle_latch_state,
					int *dash_paddle_latch_state);
extern bool cw_is_keyer_busy(void);
extern int cw_wait_for_keyer_element(void);
extern int cw_wait_for_keyer(void);
extern void cw_reset_keyer(void);


/* Straight key */
extern int cw_notify_straight_key_event(int key_state);
extern int cw_get_straight_key_state(void);
extern bool cw_is_straight_key_busy(void);
extern void cw_reset_straight_key(void);




/* deprecated functions */
extern int cw_check_representation(const char *representation)           __attribute__ ((deprecated));   /* Use cw_representation_is_valid(). */
extern int cw_lookup_representation(const char *representation, char *c) __attribute__ ((deprecated));   /* Use cw_representation_to_character(). */
extern int cw_lookup_character(char c, char *representation)             __attribute__ ((deprecated));   /* Use cw_character_to_representation(). */
extern int cw_check_character(char c)                                    __attribute__ ((deprecated));   /* Use cw_character_is_valid(). */
extern int cw_check_string(const char *string)                           __attribute__ ((deprecated));   /* Use cw_string_is_valid(). */


#if defined(__cplusplus)
}
#endif
#endif  /* _LIBCW_H */
