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

#include "config.h"

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> /* sleep() */

#include "libcw.h"

#include "libcw_test.h"

#include "libcw_debug.h"
#include "libcw_data.h"
#include "libcw_tq.h"

#include "libcw_null.h"
#include "libcw_console.h"
#include "libcw_oss.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;


typedef bool (*predicate_t)(const char *device);
static void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate);





int main(int argc, char *const argv[])
{

#define CW_SYSTEMS_MAX 5
	char sound_systems[CW_SYSTEMS_MAX + 1];

	/* TODO: modules aren't necessary here. Make cw_test_args() accept modules==NULL. */
#define CW_MODULES_MAX 5
	char modules[CW_MODULES_MAX + 1];

	if (!cw_test_args(argc, argv, sound_systems, CW_SYSTEMS_MAX, modules, CW_MODULES_MAX)) {
		cw_test_print_help(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (strstr(sound_systems, "n")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with null output\n");
		main_helper(CW_AUDIO_NULL,    "Null",        CW_DEFAULT_NULL_DEVICE,      cw_is_null_possible);
	}

	if (strstr(sound_systems, "c")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with console output\n");
		main_helper(CW_AUDIO_CONSOLE, "console",     CW_DEFAULT_CONSOLE_DEVICE,   cw_is_console_possible);
	}

	if (strstr(sound_systems, "o")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with OSS output\n");
		main_helper(CW_AUDIO_OSS,     "OSS",         CW_DEFAULT_OSS_DEVICE,       cw_is_oss_possible);
	}

	if (strstr(sound_systems, "a")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with ALSA output\n");
		main_helper(CW_AUDIO_ALSA,    "ALSA",        CW_DEFAULT_ALSA_DEVICE,      cw_is_alsa_possible);
	}

	if (strstr(sound_systems, "p")) {
		fprintf(stderr, "========================================\n");
		fprintf(stderr, "libcw: testing with PulseAudio output\n");
		main_helper(CW_AUDIO_PA,      "PulseAudio",  CW_DEFAULT_PA_DEVICE,        cw_is_pa_possible);
	}

	sleep(2);

	return 0;
}





void main_helper(int audio_system, const char *name, const char *device, predicate_t predicate)
{
	int rv = CW_FAILURE;

	rv = predicate(device);
	if (rv == CW_SUCCESS) {
		rv = cw_generator_new(audio_system, device);
		if (rv == CW_SUCCESS) {
			cw_reset_send_receive_parameters();
			cw_set_send_speed(12);
			cw_generator_start();

			//cw_send_string("abcdefghijklmnopqrstuvwyz0123456789");
			cw_send_string("eish ");
			cw_wait_for_tone_queue();

			cw_send_string("two");
			cw_wait_for_tone_queue();

			cw_send_string("three");
			cw_wait_for_tone_queue();

			cw_wait_for_tone_queue();
			cw_generator_stop();
			cw_generator_delete();
		} else {
			cw_debug_msg ((&cw_debug_object), CW_DEBUG_GENERATOR, CW_DEBUG_ERROR,
				      "libcw: can't create %s generator", name);
		}
	} else {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_SOUND_SYSTEM, CW_DEBUG_ERROR,
			      "libcw: %s output is not available", name);
	}
}
