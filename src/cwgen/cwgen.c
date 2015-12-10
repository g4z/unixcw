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
#include <sys/time.h> /* gettimeofday() */
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h> /* SCNu64 in sscanf() */

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "i18n.h"
#include "cmdline.h"
#include "cw_copyright.h"
#include "memory.h"





#define MIN_GROUPS             1   /* Lowest number of groups allowed. */
#define INITIAL_GROUPS       128   /* Default number of groups. */
#define MIN_GROUP_SIZE         1   /* Lowest group size allowed. */
#define INITIAL_GROUP_SIZE     5   /* Default group size. */
#define INITIAL_REPEAT         0   /* Default repeat count. */
#define MIN_REPEAT             0   /* Lowest repeat count allowed. */
#define MIN_LIMIT              0   /* Lowest character count limit allowed. */
#define INITIAL_LIMIT          0   /* Default character count limit. */


static const char *const DEFAULT_CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


struct cwgen_config {
	char *program_name;    /* Program's name (argv[0]) */

	int n_groups;          /* How many unique groups to generate. */
	int group_size_min;    /* Minimal size (length) of generated groups. */
	int group_size_max;    /* Maximal size (length) of generated groups. */
        int n_repeats;         /* How many times in a row to repeat each unique group; zero for no repeats (group will be printed only once). */
	uint64_t n_chars_max;  /* Maximal number of characters (excluding spaces) to generate in whole set of groups; may be zero - no limit. */

	char *charset;         /* Set of chars to be used to generate groups. */
} config = {
	.program_name   = (char *) NULL,

	.n_groups       = INITIAL_GROUPS,
	.group_size_min = INITIAL_GROUP_SIZE,
        .group_size_max = INITIAL_GROUP_SIZE,
        .n_repeats      = INITIAL_REPEAT,
        .n_chars_max    = INITIAL_LIMIT,

	.charset        = (char *) NULL
};


static const char *all_options = "g:|groups,n:|groupsize,r:|repeat,x:|limit,c:|charset,h|help,v|version";

static void cwgen_generate_characters(struct cwgen_config *config);
static void cwgen_print_usage(const char *program_name);
static void cwgen_print_help(const char *program_name);
static void cwgen_parse_command_line(int argc, char **argv, struct cwgen_config *config);
static void cwgen_free_config(struct cwgen_config *config);




/**
   \brief Generate random characters on stdout

   Generate random characters on stdout, in groups as requested, and up
   to the requested number of groups.  Characters are selected from the
   set given at random.

   \param config - program's configuration variable
*/
void cwgen_generate_characters(struct cwgen_config *config)
{
	static bool is_initialized = false;

	/* On first (usually only) call, seed the random number generator. */
	if (!is_initialized) {

		/* Previously the initializator used value returned by
		time(). Consecutive calls of the program within one
		second resulted in the same generated string - not
		very random. To improve randomness, I've switched to
		gettimeofday(). */
		struct timeval t;
		gettimeofday(&t, NULL);

		srandom(t.tv_usec);
		is_initialized = true;
	}

	/* Allocate the buffer for repeating groups. */
	char *buffer = (char *) malloc(config->group_size_max);
	if (!buffer) {
		fprintf(stderr, "%s: failed to allocate memory\n", config->program_name);
		exit(EXIT_FAILURE);
	}

	/* Generate groups up to the number requested or to the character limit. */
	int charset_length = strlen(config->charset);
	uint64_t chars = 0;

	for (int group = 0; group < config->n_groups; group++) {

		/* Randomize the group size between min and max inclusive. */
		int group_size = config->group_size_min + random() % (config->group_size_max - config->group_size_min + 1);

		/* Create random group. */
		for (int i = 0; i < group_size; i++) {
			buffer[i] = config->charset[random() % charset_length];
		}

		/* Repeatedly print the group as requested.
		   It's always printed at least once, then repeated
		   for the desired repeat count.  Break altogether if
		   we hit any set limit on printed characters. */
		int repeat = 0;
		do {
			for (int i = 0; i < group_size; i++) {

				putchar(buffer[i]);
				fflush(stdout);

				chars++;

				if (config->n_chars_max && chars >= config->n_chars_max) {
					break;
				}
			}

			putchar(' ');
			fflush(stdout);

			if (config->n_chars_max && chars >= config->n_chars_max) {
				break;
			}

		} while (repeat++ < config->n_repeats);

		if (config->n_chars_max && chars >= config->n_chars_max) {
			break;
		}
	}

	free(buffer);

	return;
}





/**
   \brief Print out a brief message directing the user to the help function

   \param program_name - program's name
*/
void cwgen_print_usage(const char *program_name)
{
	const char *format = has_longopts()
		? _("Try '%s --help' for more information.\n")
		: _("Try '%s -h' for more information.\n");

	fprintf(stderr, format, program_name);
	return;
}





/*
  \brief Print out a brief page of help information

  \param program_name - program's name
*/
static void cwgen_print_help(const char *program_name)
{
	if (!has_longopts()) {
		fprintf(stderr, "%s", _("Long format of options is not supported on your system\n\n"));
	}

	printf(_("Usage: %s [options...]\n\n"), program_name);

	printf(_("  -g, --groups=GROUPS    send GROUPS groups of chars [default %d]\n"), INITIAL_GROUPS);
	printf(_("                         GROUPS values may not be lower than %d\n"), MIN_GROUPS);
	printf(_("  -n, --groupsize=GS     make groups GS chars [default %d]\n"), INITIAL_GROUP_SIZE);
	printf(_("                         GS values may not be lower than %d, or\n"), MIN_GROUP_SIZE);
	printf("%s", _("  -n, --groupsize=GL-GH  make groups between GL and GH chars\n"));
	printf("%s", _("                         valid GL, GH values are as for GS above\n"));
	printf(_("  -r, --repeat=COUNT     repeat each group COUNT times [default %d]\n"), INITIAL_REPEAT);
	printf(_("                         COUNT values may not be lower than %d\n"), MIN_REPEAT);
	printf("%s", _("  -c, --charset=CHARSET  select chars to send from this set\n"));
	printf(_("                         [default %s]\n"), DEFAULT_CHARSET);
	printf(_("  -x, --limit=LIMIT      stop after LIMIT characters [default %d]\n"), INITIAL_LIMIT);
	printf("%s", _("                         a LIMIT of zero indicates no set limit\n"));
	printf("%s", _("  -h, --help             print this message\n"));
	printf("%s", _("  -v, --version          output version information and exit\n\n"));

	exit(EXIT_SUCCESS);
}





/**
   \brief Parse command line options

   Parse the command line options for initial values for the various
   global and flag definitions.

   \param argc - main()'s argc
   \param argv - main()'s argv
   \param config - program's configuration variable
*/
void cwgen_parse_command_line(int argc, char **argv, struct cwgen_config *config)
{
	int option;
	char *argument;

	config->program_name = strdup(cw_program_basename(argv[0]));
	if (!config->program_name) {
		fprintf(stderr, "%s: failed to allocate memory\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	while (get_option(argc, argv, all_options,
			  &option, &argument)) {

		switch (option) {
		case 'g':
			if (sscanf(argument, "%d", &(config->n_groups)) != 1
			    || config->n_groups < MIN_GROUPS) {

				fprintf(stderr, _("%s: invalid groups value: '%s'\n"), config->program_name, argument);
				exit(EXIT_FAILURE);
			}
			break;

		case 'n':
			if (sscanf(argument, "%d-%d", &(config->group_size_min), &(config->group_size_max)) == 2) {
				if (config->group_size_min < MIN_GROUP_SIZE
				    || config->group_size_max < MIN_GROUP_SIZE
				    || config->group_size_min > config->group_size_max) {

					fprintf(stderr, _("%s: invalid groupsize range: '%s'\n"), config->program_name, argument);
					exit(EXIT_FAILURE);
				}
			} else if (sscanf(argument, "%d", &(config->group_size_min)) == 1) {
				if (config->group_size_min < MIN_GROUP_SIZE) {

					fprintf(stderr, _("%s: invalid groupsize value\n"), config->program_name);
					exit(EXIT_FAILURE);
				}
				config->group_size_max = config->group_size_min;
			}
			break;

		case 'r':
			if (sscanf(argument, "%d", &(config->n_repeats)) != 1
			    || config->n_repeats < MIN_REPEAT) {

				fprintf(stderr, _("%s: invalid repeat value: '%s'\n"), config->program_name, argument);
				exit(EXIT_FAILURE);
			}
			break;

		case 'x':
			if (sscanf(argument, "%" SCNu64, &(config->n_chars_max)) != 1
			    || strstr(argument, "-")) {

				fprintf(stderr, _("%s: invalid limit value: %s\n"), config->program_name, argument);
				exit(EXIT_FAILURE);
			}
			fprintf(stderr, _("%s: valid limit value: %s\n"), config->program_name, argument);
			break;

		case 'c':
			if (strlen(argument) == 0) {
				fprintf(stderr, _("%s: charset cannot be empty\n"), config->program_name);
				exit(EXIT_FAILURE);
			}
			assert(!config->charset);
			config->charset = strdup(argument);
			if (!config->charset) {
				fprintf(stderr, _("%s: failed to allocate memory\n"), config->program_name);
				exit(EXIT_FAILURE);
			}
			break;

		case 'h':
			cwgen_print_help(config->program_name);

		case 'v':
			printf(_("%s version %s\n%s\n"),
			       config->program_name, PACKAGE_VERSION, _(CW_COPYRIGHT));
			exit(EXIT_SUCCESS);

		case '?':
			cwgen_print_usage(config->program_name);
			exit(EXIT_FAILURE);

		default:
			fprintf(stderr, _("%s: getopts returned %c\n"), config->program_name, option);
			exit(EXIT_FAILURE);
		}
	}

	if (get_optind() != argc) {
		cwgen_print_usage(config->program_name);
		exit(EXIT_FAILURE);
	}

	return;
}





/**
   \brief Parse the command line options, then generate the characters requested
*/
int main(int argc, char **argv)
{
	int combined_argc;
	char **combined_argv;

	/* Set locale and message catalogs. */
	i18n_initialize();

	/* Parse combined environment and command line arguments. */
	combine_arguments(_("CWGEN_OPTIONS"),
			  argc, argv, &combined_argc, &combined_argv);
	cwgen_parse_command_line(combined_argc, combined_argv, &config);

	if (!config.charset) {
		config.charset = strdup(DEFAULT_CHARSET);
		if (!config.charset) {
			fprintf(stderr, _("%s: failed to allocate memory\n"), argv[0]);
			return EXIT_FAILURE;
		}
	}

	/* Generate the character groups as requested. */
	cwgen_generate_characters(&config);
	putchar('\n');

	cwgen_free_config(&config);

	return EXIT_SUCCESS;
}





/**
   \brief Deallocate memory used by fields of config variable

   Function calls free() for all pointers to previously allocated memory.

   \param config - pointer to config variable
*/
void cwgen_free_config(struct cwgen_config *config)
{
	if (config->charset) {
		free(config->charset);
		config->charset = (char *) NULL;
	}

	if (config->program_name) {
		free(config->program_name);
		config->program_name = (char *) NULL;
	}

	return;
}
