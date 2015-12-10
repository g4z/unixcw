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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#if defined(HAVE_GETOPT_H)
# include <getopt.h>
#endif

#include "libcw.h"
#include "cmdline.h"
#include "i18n.h"
#include "memory.h"
#include "cw_copyright.h"


static int cw_process_option(int opt, const char *optarg, cw_config_t *config);
static void cw_print_usage(const char *program_name);


/*---------------------------------------------------------------------*/
/*  Command line helpers                                               */
/*---------------------------------------------------------------------*/





/**
   \brief Return the program's base name from the given argv0

   Function returns pointer to substring in argv[0], so I guess that the
   pointer is owned by environment (?).
   Since there is always some non-NULL argv[0], the function always returns
   non-NULL pointer.

   \param argv0 - first argument to the program, argv[0]

   \return program's name
*/
const char *cw_program_basename(const char *argv0)
{
	const char *base = strrchr(argv0, '/');
	return base ? base + 1 : argv0;
}





/**
   \brief Combine command line options and environment options

   Build a new argc and argv by combining command line and environment
   options.

   The new values are held in the heap, and the malloc'ed addresses
   are not retained, so do not call this function repeatedly,
   otherwise it will leak memory.

   Combined values are returned through \p new_argc and \p new_argv.

   \param env_variable - name of environment variable to read from
   \param argc - argc of program's main()
   \param argv - argv[] of program's main()
   \param new_argc - combined argc
   \param new_argv[] - combined argv
*/
void combine_arguments(const char *env_variable,
		       int argc, char *const argv[],
		       /* out */ int *new_argc, /* out */ char **new_argv[])
{
	/* Begin with argv[0], which stays in place. */
	char **local_argv = safe_malloc(sizeof (*local_argv));
	int local_argc = 0;
	local_argv[local_argc++] = argv[0];

	/* If options are given in an environment variable, add these next. */
	char *env_options = getenv(env_variable);
	if (env_options) {
		char *options = safe_strdup(env_options);
		for (char *option = strtok(options, " \t");
		     option;
		     option = strtok(NULL, " \t")) {

			local_argv = safe_realloc(local_argv,
						  sizeof (*local_argv) * (local_argc + 1));
			local_argv[local_argc++] = option;
		}
	}

	/* Append the options given on the command line itself. */
	for (int arg = 1; arg < argc; arg++) {
		local_argv = safe_realloc(local_argv,
					  sizeof (*local_argv) * (local_argc + 1));
		local_argv[local_argc++] = argv[arg];
	}

	/* Return the constructed argc/argv. */
	*new_argc = local_argc;
	*new_argv = local_argv;

	return;
}





/*---------------------------------------------------------------------*/
/*  Option handling helpers                                            */
/*---------------------------------------------------------------------*/





/**
   \brief Check if target system supports long options

   \return true the system supports long options,
   \return false otherwise
*/
bool has_longopts(void)
{
#if defined(HAVE_GETOPT_LONG)
	return true;
#else
	return false;
#endif
}





/**
   \brief Adapter wrapper round getopt() and getopt_long()

   Descriptor strings are comma-separated groups of elements of the
   form "c[:]|longopt", giving the short form option ('c'), ':' if it
   requires an argument, and the long form option.

   \param argc
   \param argv
   \param descriptor
   \param option
   \param argument

   \return true if there are still options in argv to be drawn
   \return false if argv is exhausted
*/
int get_option(int argc, char *const argv[],
	       const char *descriptor,
	       int *option, char **argument)
{
	static char *option_string = NULL;          /* Standard getopt() string */
#if defined(HAVE_GETOPT_LONG)
	static struct option *long_options = NULL;  /* getopt_long() structure */
	static char **long_names = NULL;            /* Allocated names array */
	static int long_count = 0;                  /* Entries in long_options */
#endif

	int opt;

	/* If this is the first call, build a new option_string and a
	   matching set of long options.  */
	if (!option_string) {
		/* Begin with an empty short options string. */
		option_string = safe_strdup("");

		/* Break the descriptor into comma-separated elements. */
		char *options = safe_strdup(descriptor);
		for (char *element = strtok(options, ",");
		     element;
		     element = strtok(NULL, ",")) {

			/* Determine if this option requires an argument. */
			int needs_arg = element[1] == ':';

			/* Append the short option character, and ':'
			   if present, to the short options string.
			   For simplicity in reallocating, assume that
			   the ':' is always there. */
			option_string = safe_realloc(option_string,
						      strlen(option_string) + 3);
			strncat(option_string, element, needs_arg ? 2 : 1);

#if defined(HAVE_GETOPT_LONG)
			/* Take a copy of the long name and add it to
			   a retained array.  Because struct option
			   makes name a const char*, we can't just
			   store it in there and then free later. */
			long_names = safe_realloc(long_names,
						  sizeof (*long_names) * (long_count + 1));
			long_names[long_count] = safe_strdup(element + (needs_arg ? 3 : 2));

			/* Add a new entry to the long options array. */
			long_options = safe_realloc(long_options,
						     sizeof (*long_options) * (long_count + 2));
			long_options[long_count].name = long_names[long_count];
			long_options[long_count].has_arg = needs_arg;
			long_options[long_count].flag = NULL;
			long_options[long_count].val = element[0];
			long_count++;

			/* Set the end sentry to all zeroes. */
			memset(long_options + long_count, 0, sizeof (*long_options));
#endif
		}

		free(options);
	}

	/* Call the appropriate getopt function to get the first/next
	   option. */
#if defined(HAVE_GETOPT_LONG)
	opt = getopt_long(argc, argv, option_string, long_options, NULL);
#else
	opt = getopt(argc, argv, option_string);
#endif

	/* If no more options, clean up allocated memory before
	   returning. */
	if (opt == -1) {
#if defined(HAVE_GETOPT_LONG)

		/* Free each long option string created above, using
		   the long_names growable array because the
		   long_options[i].name aliased to it is a const
		   char*.  Then free long_names itself, and reset
		   pointer. */
		for (int i = 0; i < long_count; i++) {
			free(long_names[i]);
			long_names[i] = NULL;
		}

		free(long_names);
		long_names = NULL;

		/* Free the long options structure, and reset pointer
		   and counter. */
		free(long_options);
		long_options = NULL;
		long_count = 0;
#endif
		/* Free and reset the retained short options string. */
		free(option_string);
		option_string = NULL;
	}

	/* Return the option and argument, with false if no more
	   arguments. */
	*option = opt;
	*argument = optarg;
	return !(opt == -1);
}





/**
   Return the value of getopt()'s optind after get_options() calls complete.
*/
int get_optind(void)
{
	return optind;
}





void cw_print_help(cw_config_t *config)
{
	/* int format = has_longopts() */
	fprintf(stderr, _("Usage: %s [options...]\n"), config->program_name);

	if (!has_longopts()) {
		fprintf(stderr, "%s", _("Long format of options is not supported on your system\n\n"));
	}

	fprintf(stderr, "%s", _("Audio system options:\n"));
	fprintf(stderr, "%s", _("  -s, --system=SYSTEM\n"));
	fprintf(stderr, "%s", _("        generate sound using SYSTEM audio system\n"));
	fprintf(stderr, "%s", _("        SYSTEM: {null|console|oss|alsa|pulseaudio|soundcard}\n"));
	fprintf(stderr, "%s", _("        'null': don't use any sound output\n"));
	fprintf(stderr, "%s", _("        'console': use system console/buzzer\n"));
	fprintf(stderr, "%s", _("               this output may require root privileges\n"));
	fprintf(stderr, "%s", _("        'oss': use OSS output\n"));
	fprintf(stderr, "%s", _("        'alsa' use ALSA output\n"));
	fprintf(stderr, "%s", _("        'pulseaudio' use PulseAudio output\n"));
	fprintf(stderr, "%s", _("        'soundcard': use either PulseAudio, OSS or ALSA\n"));
	fprintf(stderr, "%s", _("        default sound system: 'pulseaudio'->'oss'->'alsa'\n\n"));
	fprintf(stderr, "%s", _("  -d, --device=DEVICE\n"));
	fprintf(stderr, "%s", _("        use DEVICE as output device instead of default one;\n"));
	fprintf(stderr, "%s", _("        optional for {console|oss|alsa|pulseaudio};\n"));
	fprintf(stderr, "%s", _("        default devices are:\n"));
	fprintf(stderr,       _("        'console': \"%s\"\n"), CW_DEFAULT_CONSOLE_DEVICE);
	fprintf(stderr,       _("        'oss': \"%s\"\n"), CW_DEFAULT_OSS_DEVICE);
	fprintf(stderr,       _("        'alsa': \"%s\"\n"), CW_DEFAULT_ALSA_DEVICE);
	fprintf(stderr,       _("        'pulseaudio': %s\n\n"), CW_DEFAULT_PA_DEVICE);

	fprintf(stderr, "%s", _("Sending options:\n"));

	fprintf(stderr, "%s", _("  -w, --wpm=WPM          set initial words per minute\n"));
	fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_SPEED_MIN, CW_SPEED_MAX);
	fprintf(stderr,       _("                         default value: %d\n"), CW_SPEED_INITIAL);
	fprintf(stderr, "%s", _("  -t, --tone=HZ          set initial tone to HZ\n"));
	fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_FREQUENCY_MIN, CW_FREQUENCY_MAX);
	fprintf(stderr,       _("                         default value: %d\n"), CW_FREQUENCY_INITIAL);
	fprintf(stderr, "%s", _("  -v, --volume=PERCENT   set initial volume to PERCENT\n"));
	fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_VOLUME_MIN, CW_VOLUME_MAX);
	fprintf(stderr,       _("                         default value: %d\n"), CW_VOLUME_INITIAL);

	fprintf(stderr, "%s", _("Dot/dash options:\n"));
	fprintf(stderr, "%s", _("  -g, --gap=GAP          set extra gap between letters\n"));
	fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_GAP_MIN, CW_GAP_MAX);
	fprintf(stderr,       _("                         default value: %d\n"), CW_GAP_INITIAL);
	fprintf(stderr, "%s", _("  -k, --weighting=WEIGHT set weighting to WEIGHT\n"));
	fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_WEIGHTING_MIN, CW_WEIGHTING_MAX);
	fprintf(stderr,       _("                         default value: %d\n"), CW_WEIGHTING_INITIAL);

	fprintf(stderr, "%s",     _("Other options:\n"));
	if (config->is_cw) {
		fprintf(stderr, "%s", _("  -e, --noecho           disable sending echo to stdout\n"));
		fprintf(stderr, "%s", _("  -m, --nomessages       disable writing messages to stderr\n"));
		fprintf(stderr, "%s", _("  -c, --nocommands       disable executing embedded commands\n"));
		fprintf(stderr, "%s", _("  -o, --nocombinations   disallow [...] combinations\n"));
		fprintf(stderr, "%s", _("  -p, --nocomments       disallow {...} comments\n"));
	}
	if (config->has_practice_time) {
		fprintf(stderr, "%s", _("  -T, --time=TIME        set initial practice time (in minutes)\n"));
		fprintf(stderr,       _("                         valid values: %d - %d\n"), CW_PRACTICE_TIME_MIN, CW_PRACTICE_TIME_MAX);
		fprintf(stderr,       _("                         default value: %d\n"), CW_PRACTICE_TIME_INITIAL);
	}
	if (config->has_infile) {
		fprintf(stderr, "%s", _("  -f, --infile=FILE      read practice words from FILE\n"));
	}
	if (config->has_outfile) {
		fprintf(stderr, "%s", _("  -F, --outfile=FILE     write current practice words to FILE\n"));
	}
	if (config->is_cw) {
		fprintf(stderr, "%s", _("                         default file: stdin\n"));
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "%s", _("  -h, --help             print this message\n"));
	fprintf(stderr, "%s", _("  -V, --version          print version information\n\n"));

	return;
}





int cw_process_argv(int argc, char *const argv[], const char *options, cw_config_t *config)
{
	int option;
	char *argument;

	while (get_option(argc, argv, options, &option, &argument)) {
		if (!cw_process_option(option, argument, config)) {
			return CW_FAILURE;
		}
	}

	if (get_optind() != argc) {
		fprintf(stderr, "%s: expected argument after options\n", config->program_name);
		cw_print_usage(config->program_name);
		return CW_FAILURE;
	} else {
		return CW_SUCCESS;
	}
}





int cw_process_option(int opt, const char *optarg, cw_config_t *config)
{
	switch (opt) {
	case 's':
		if (!strcmp(optarg, "null")
		    || !strcmp(optarg, "n")) {

			config->audio_system = CW_AUDIO_NULL;
		} else if (!strcmp(optarg, "alsa")
		    || !strcmp(optarg, "a")) {

			config->audio_system = CW_AUDIO_ALSA;
		} else if (!strcmp(optarg, "oss")
			   || !strcmp(optarg, "o")) {

			config->audio_system = CW_AUDIO_OSS;
		} else if (!strcmp(optarg, "pulseaudio")
			   || !strcmp(optarg, "p")) {

			config->audio_system = CW_AUDIO_PA;
		} else if (!strcmp(optarg, "console")
			   || !strcmp(optarg, "c")) {

			config->audio_system = CW_AUDIO_CONSOLE;

		} else if (!strcmp(optarg, "soundcard")
			   || !strcmp(optarg, "s")) {

			config->audio_system = CW_AUDIO_SOUNDCARD;
		} else {
			fprintf(stderr, "%s: invalid audio system (option 's'): %s\n", config->program_name, optarg);
			return CW_FAILURE;
		}
		break;

	case 'd':
		// fprintf(stderr, "%s: d:%s\n", config->program_name, optarg);
		if (optarg && strlen(optarg)) {
			config->audio_device = strdup(optarg);
		} else {
			fprintf(stderr, "%s: no device specified for option -d\n", config->program_name);
			return CW_FAILURE;
		}
		break;

	case 'w':
		{
			// fprintf(stderr, "%s: w:%s\n", config->program_name, optarg);
			int speed = atoi(optarg);
			if (speed < CW_SPEED_MIN || speed > CW_SPEED_MAX) {
				fprintf(stderr, "%s: speed out of range: %d\n", config->program_name, speed);
				return CW_FAILURE;
			} else {
				config->send_speed = speed;
			}
			break;
		}

	case 't':
		{
			// fprintf(stderr, "%s: t:%s\n", config->program_name, optarg);
			int frequency = atoi(optarg);
			if (frequency < CW_FREQUENCY_MIN || frequency > CW_FREQUENCY_MAX) {
				fprintf(stderr, "%s: frequency out of range: %d\n", config->program_name, frequency);
				return CW_FAILURE;
			} else {
				config->frequency = frequency;
			}
			break;
		}

	case 'v':
		{
			// fprintf(stderr, "%s: v:%s\n", config->program_name, optarg);
			int volume = atoi(optarg);
			if (volume < CW_VOLUME_MIN || volume > CW_VOLUME_MAX) {
				fprintf(stderr, "%s: volume level out of range: %d\n", config->program_name, volume);
				return CW_FAILURE;
			} else {
				config->volume = volume;
			}
			break;
		}

	case 'g':
		{
			// fprintf(stderr, "%s: g:%s\n", config->program_name, optarg);
			int gap = atoi(optarg);
			if (gap < CW_GAP_MIN || gap > CW_GAP_MAX) {
				fprintf(stderr, "%s: gap out of range: %d\n", config->program_name, gap);
				return CW_FAILURE;
			} else {
				config->gap = gap;
			}
			break;
		}

	case 'k':
		{
			// fprintf(stderr, "%s: k:%s\n", config->program_name, optarg);
			int weighting = atoi(optarg);
			if (weighting < CW_WEIGHTING_MIN || weighting > CW_WEIGHTING_MAX) {
				fprintf(stderr, "%s: weighting out of range: %d\n", config->program_name, weighting);
				return CW_FAILURE;
			} else {
				config->weighting = weighting;
			}
			break;
		}

	case 'T':
		{
			// fprintf(stderr, "%s: T:%s\n", config->program_name, optarg);
			int time = atoi(optarg);
			if (time < 0) {
				fprintf(stderr, "%s: practice time is negative\n", config->program_name);
				return CW_FAILURE;
			} else {
				config->practice_time = time;
			}
			break;
		}

	case 'f':
		if (optarg && strlen(optarg)) {
			config->input_file = strdup(optarg);
		} else {
			fprintf(stderr, "%s: no input file specified for option -f\n", config->program_name);
			return CW_FAILURE;
		}
		/* TODO: access() */
		break;

	case 'F':
		if (optarg && strlen(optarg)) {
			config->output_file = strdup(optarg);
		} else {
			fprintf(stderr, "%s: no output file specified for option -F\n", config->program_name);
			return CW_FAILURE;
		}
		/* TODO: access() */
		break;

        case 'e':
		config->do_echo = false;
		break;

        case 'm':
		config->do_errors = false;
		break;

        case 'c':
		config->do_commands = false;
		break;

        case 'o':
		config->do_combinations = false;
		break;

        case 'p':
		config->do_comments = false;
		break;

	case 'h':
		cw_print_help(config);
		exit(EXIT_SUCCESS);
	case 'V':
		fprintf(stderr, _("%s version %s\n"), config->program_name, PACKAGE_VERSION);
		fprintf(stderr, "%s\n", CW_COPYRIGHT);
		exit(EXIT_SUCCESS);
	case '?':
	default: /* '?' */
		cw_print_usage(config->program_name);
		return CW_FAILURE;
	}

	return CW_SUCCESS;
}




void cw_print_usage(const char *program_name)
{
	const char *format = has_longopts()
		? _("Try '%s --help' for more information.\n")
		: _("Try '%s -h' for more information.\n");

	fprintf(stderr, format, program_name);
	return;
}
