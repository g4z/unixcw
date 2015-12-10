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
   \file cw_common.c

   Code that is common for all _applications_ from unixcw package.
   Wrappers for some libcw functions, that probably don't belong to libcw.c.
*/

#define _BSD_SOURCE /* strdup() */

#include "config.h"

#include <stdio.h>  /* fprintf(stderr, ...) */
#include <stdlib.h> /* malloc() / free() */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif


#include "libcw.h"
#include "cw_common.h"

static int cw_generator_apply_config(cw_config_t *config);





/**
   Create new configuration with default values

   Function returns pointer to config variable, with fields
   of config initialized to valid default values.

   \param program_name - human-readable name of application calling the function

   \return pointer to config on success
   \return NULL on failure
*/
cw_config_t *cw_config_new(const char *program_name)
{
	cw_config_t *config = (cw_config_t *) malloc(sizeof (cw_config_t));
	if (!config) {
		fprintf(stderr, "%s: can't allocate memory for configuration\n", program_name);
		return NULL;
	}

	config->program_name = strdup(program_name);
	if (!(config->program_name)) {
		fprintf(stderr, "%s: can't allocate memory for program name\n", program_name);

		free(config);
		config = NULL;

		return NULL;
	}

	config->audio_system = CW_AUDIO_NONE;
	config->audio_device = NULL;
	config->send_speed = CW_SPEED_INITIAL;
	config->frequency = CW_FREQUENCY_INITIAL;
	config->volume = CW_VOLUME_INITIAL;
	config->gap = CW_GAP_INITIAL;
	config->weighting = CW_WEIGHTING_INITIAL;
	config->practice_time = CW_PRACTICE_TIME_INITIAL;
	config->input_file = NULL;
	config->output_file = NULL;

	config->is_cw = false;
	config->has_practice_time = false;
	config->has_outfile = false;
	config->has_infile = true;

	config->do_echo = true;
	config->do_errors = true;
	config->do_commands = true;
	config->do_combinations = true;
	config->do_comments = true;

	return config;
}





/**
   \brief Delete configuration variable

   Deallocate given configuration, assign NULL to \p config

   \param config - configuration variable to deallocate
*/
void cw_config_delete(cw_config_t **config)
{
	if (*config) {
		if ((*config)->program_name) {
			free((*config)->program_name);
			(*config)->program_name = NULL;
		}
		if ((*config)->audio_device) {
			free((*config)->audio_device);
			(*config)->audio_device = NULL;
		}
		if ((*config)->input_file) {
			free((*config)->input_file);
			(*config)->input_file = NULL;
		}
		if ((*config)->output_file) {
			free((*config)->output_file);
			(*config)->output_file = NULL;
		}
		free(*config);
		*config = NULL;
	}

	return;
}





/**
   \brief Validate configuration

   Check consistency and correctness of configuration.

   Currently the function only checks if "audio device" command line
   argument has been specified at the same time when "soundcard"
   has been specified as audio system. This is an inconsistency as
   you can specify audio device only for specific audio system ("soundcard"
   is just a general audio system).

   \param config - configuration to validate

   \return true if configuration is valid
   \return false if configuration is invalid
*/
int cw_config_is_valid(cw_config_t *config)
{
	/* Deal with odd argument combinations. */
        if (config->audio_device) {
		if (config->audio_system == CW_AUDIO_SOUNDCARD) {
			fprintf(stderr, "libcw: a device has been specified for 'soundcard' sound system\n");
			fprintf(stderr, "libcw: a device can be specified only for 'console', 'oss', 'alsa' or 'pulseaudio'\n");
			return false;
		} else if (config->audio_system == CW_AUDIO_NULL) {
			fprintf(stderr, "libcw: a device has been specified for 'null' sound system\n");
			fprintf(stderr, "libcw: a device can be specified only for 'console', 'oss', 'alsa' or 'pulseaudio'\n");
			return false;
		} else {
			; /* audio_system is one that accepts custom "audio device" */
		}
	} else {
		; /* no custom "audio device" specified, a default will be used */
	}

	return true;
}





/**
   \brief Create new cw generator, apply given configuration

   Create new cw generator (using audio system from given \p config), and
   then apply rest of parameters from \p config to that generator.

   \p config should be first created with cw_config_new().

   \param config - configuration to be applied to generator

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_generator_new_from_config(cw_config_t *config)
{
	if (config->audio_system == CW_AUDIO_NULL) {
		if (cw_is_null_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_NULL, config->audio_device)) {
				if (cw_generator_apply_config(config)) {
					return CW_SUCCESS;
				} else {
					fprintf(stderr, "%s: failed to apply configuration\n", config->program_name);
					return CW_FAILURE;
				}
			} else {
				fprintf(stderr, "%s: failed to open Null output\n", config->program_name);
			}
		} else {
			fprintf(stderr, "%s: Null output not available\n", config->program_name);
		}
		/* fall through to try with next audio system type */
	}


	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_PA
	    || config->audio_system == CW_AUDIO_SOUNDCARD) {

		if (cw_is_pa_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_PA, config->audio_device)) {
				if (cw_generator_apply_config(config)) {
					return CW_SUCCESS;
				} else {
					fprintf(stderr, "%s: failed to apply configuration\n", config->program_name);
					return CW_FAILURE;
				}
			} else {
				fprintf(stderr, "%s: failed to open PulseAudio output\n", config->program_name);
			}
		} else {
			fprintf(stderr, "%s: PulseAudio output not available (device: %s)\n",
				config->program_name,
				config->audio_device ? config->audio_device : CW_DEFAULT_PA_DEVICE);
		}
		/* fall through to try with next audio system type */
	}

	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_OSS
	    || config->audio_system == CW_AUDIO_SOUNDCARD) {

		if (cw_is_oss_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_OSS, config->audio_device)) {
				if (cw_generator_apply_config(config)) {
					return CW_SUCCESS;
				} else {
					fprintf(stderr, "%s: failed to apply configuration\n", config->program_name);
					return CW_FAILURE;
				}
			} else {
				fprintf(stderr,
					"%s: failed to open OSS output with device \"%s\"\n",
					config->program_name, cw_get_soundcard_device());
			}
		} else {
			fprintf(stderr, "%s: OSS output not available (device: %s)\n",
				config->program_name,
				config->audio_device ? config->audio_device : CW_DEFAULT_OSS_DEVICE);
		}
		/* fall through to try with next audio system type */
	}


	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_ALSA
	    || config->audio_system == CW_AUDIO_SOUNDCARD) {

		if (cw_is_alsa_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_ALSA, config->audio_device)) {
				if (cw_generator_apply_config(config)) {
					return CW_SUCCESS;
				} else {
					fprintf(stderr, "%s: failed to apply configuration\n", config->program_name);
					return CW_FAILURE;
				}
			} else {
				fprintf(stderr,
					"%s: failed to open ALSA output with device \"%s\"\n",
					config->program_name, cw_get_soundcard_device());
			}
		} else {
			fprintf(stderr, "%s: ALSA output not available (device: %s)\n",
				config->program_name,
				config->audio_device ? config->audio_device : CW_DEFAULT_ALSA_DEVICE);
		}
		/* fall through to try with next audio system type */
	}


	if (config->audio_system == CW_AUDIO_NONE
	    || config->audio_system == CW_AUDIO_CONSOLE) {

		if (cw_is_console_possible(config->audio_device)) {
			if (cw_generator_new(CW_AUDIO_CONSOLE, config->audio_device)) {
				if (cw_generator_apply_config(config)) {
					return CW_SUCCESS;
				} else {
					fprintf(stderr, "%s: failed to apply configuration\n", config->program_name);
					return CW_FAILURE;
				}
			} else {
				fprintf(stderr,
					"%s: failed to open console output with device %s\n",
					config->program_name, cw_get_console_device() ? cw_get_console_device() : config->audio_device);
			}
		} else {
			fprintf(stderr, "%s: console output not available (device: %s)\n",
				config->program_name,
				config->audio_device ? config->audio_device : CW_DEFAULT_CONSOLE_DEVICE);
		}
		/* fall through to try with next audio system type */
	}

	/* there is no next audio system type to try */
	return CW_FAILURE;
}





/**
   \brief Apply given configuration to a generator

   Function applies frequency, volume, sending speed, gap and weighting
   to current generator. The generator should exist (it should be created
   by cw_generator_new().

   The function is just a wrapper for few common function calls, to be used
   in cw_generator_new_from_config().

   \param config - current configuration

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_generator_apply_config(cw_config_t *config)
{
	if (!cw_set_frequency(config->frequency)) {
		return CW_FAILURE;
	}
	if (!cw_set_volume(config->volume)) {
		return CW_FAILURE;
	}
	if (!cw_set_send_speed(config->send_speed)) {
		return CW_FAILURE;
	}
	if (!cw_set_gap(config->gap)) {
		return CW_FAILURE;
	}
	if (!cw_set_weighting(config->weighting)) {
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}





/**
   \brief Generate a tone that indicates a start
*/
void cw_start_beep(void)
{
	cw_flush_tone_queue();
	cw_queue_tone(20000, 500);
	cw_queue_tone(20000, 1000);
	cw_wait_for_tone_queue();
	return;
}





/**
   \brief Generate a tone that indicates an end
*/
void cw_end_beep(void)
{
      cw_flush_tone_queue();
      cw_queue_tone(20000, 500);
      cw_queue_tone(20000, 1000);
      cw_queue_tone(20000, 500);
      cw_queue_tone(20000, 1000);
      cw_wait_for_tone_queue();
      return;
}




/**
   \brief Get line from FILE

   Line of text is returned through \p buffer that should be allocated
   by caller. Total buffer size (including space for ending NUL) is
   given by \p buffer_size.

   The function adds ending NULL.

   Function strips newline character from the line read from file. The
   newline character is not put into \p buffer.

   \return true on successful reading of line
   \return false otherwise
*/
bool cw_getline(FILE *stream, char *buffer, int buffer_size)
{
	if (!feof(stream) && fgets(buffer, buffer_size, stream)) {

		size_t bytes = strlen(buffer);
		while (bytes > 0 && strchr("\r\n", buffer[bytes - 1])) {
			buffer[--bytes] = '\0';
		}

		return true;
	}

	return false;
}
