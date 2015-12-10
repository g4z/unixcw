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


/**
   \file libcw_test.c

   \brief Utility functions for test executables.
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
#include <getopt.h>
#include <assert.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw.h"
#include "libcw_debug.h"
#include "libcw_test.h"





int cw_test_args(int argc, char *const argv[],
		 char *sound_systems, size_t systems_max,
		 char *modules, size_t modules_max)
{
	strncpy(sound_systems, "ncoap", systems_max);
	sound_systems[systems_max] = '\0';

	strncpy(modules, "gtko", modules_max);
	modules[modules_max] = '\0';

	if (argc == 1) {
		fprintf(stderr, "sound systems = \"%s\"\n", sound_systems);
		fprintf(stderr, "modules = \"%s\"\n", modules);
		return CW_SUCCESS;
	}

	int opt;
	while ((opt = getopt(argc, argv, "m:s:")) != -1) {
		switch (opt) {
		case 's':
			{
				size_t len = strlen(optarg);
				if (!len || len > systems_max) {
					return CW_FAILURE;
				}

				int j = 0;
				for (size_t i = 0; i < len; i++) {
					if (optarg[i] != 'n'
					    && optarg[i] != 'c'
					    && optarg[i] != 'o'
					    && optarg[i] != 'a'
					    && optarg[i] != 'p') {

						return CW_FAILURE;
					} else {
						sound_systems[j] = optarg[i];
						j++;
					}
				}
				sound_systems[j] = '\0';
			}
			break;
		case 'm':
			{
				size_t len = strlen(optarg);
				if (!len || len > modules_max) {
					return CW_FAILURE;
				}

				int j = 0;
				for (size_t i = 0; i < len; i++) {
					if (optarg[i] != 'g'       /* Generator. */
					    && optarg[i] != 't'    /* Tone queue. */
					    && optarg[i] != 'k'    /* Morse key. */
					    && optarg[i] != 'o') { /* Other. */

						return CW_FAILURE;
					} else {
						modules[j] = optarg[i];
						j++;
					}
				}
				modules[j] = '\0';

			}

			break;
		default: /* '?' */
			return CW_FAILURE;
		}
	}

	fprintf(stderr, "sound systems = \"%s\"\n", sound_systems);
	fprintf(stderr, "modules = \"%s\"\n", modules);
	return CW_SUCCESS;
}





void cw_test_print_help(const char *progname)
{
	fprintf(stderr, "Usage: %s [-s <sound systems>] [-m <modules>]\n\n", progname);
	fprintf(stderr, "       <sound system> is one or more of those:\n");
	fprintf(stderr, "       n - null\n");
	fprintf(stderr, "       c - console\n");
	fprintf(stderr, "       o - OSS\n");
	fprintf(stderr, "       a - ALSA\n");
	fprintf(stderr, "       p - PulseAudio\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       <modules> is one or more of those:\n");
	fprintf(stderr, "       g - generator\n");
	fprintf(stderr, "       t - tone queue\n");
	fprintf(stderr, "       k - Morse key\n");
	fprintf(stderr, "       o - other\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "       If no argument is provided, the program will attempt to test all audio systems and all modules\n");

	return;
}
