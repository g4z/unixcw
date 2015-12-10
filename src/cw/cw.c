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
#define _BSD_SOURCE

#include "config.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "cw.h"
#include "libcw.h"

#include "i18n.h"
#include "cmdline.h"
#include "cw_copyright.h"


/*---------------------------------------------------------------------*/
/*  Module variables, miscellaneous other stuff                        */
/*---------------------------------------------------------------------*/


/* Forward declarations for printf-like functions with checkable arguments. */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
static void write_to_echo_stream (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
static void write_to_message_stream (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
static void write_to_cw_sender (const char *format, ...)
    __attribute__ ((__format__ (__printf__, 1, 2)));
#endif



static cw_config_t *config = NULL; /* program-specific configuration */
static bool generator = false;     /* have we created a generator? */
static const char *all_options = "s:|system,d:|device,"
	"w:|wpm,t:|tone,v:|volume,"
	"g:|gap,k:|weighting,"
	"f:|infile,"
	"e|noecho,m|nomessages,c|nocommands,o|nocombinations,p|nocomments,"
	"h|help,V|version";

static void cw_atexit(void);


/*---------------------------------------------------------------------*/
/*  Convenience functions                                              */
/*---------------------------------------------------------------------*/

/*
 * write_to_echo_stream()
 * write_to_message_stream()
 *
 * Local fprintf functions that suppress output to if the appropriate flag
 * is not set; writes are synchronously flushed.
 */
static void
write_to_echo_stream (const char *format, ...)
{
  if (config->do_echo)
    {
      va_list ap;

      va_start (ap, format);
      vfprintf (stdout, format, ap);
      fflush (stdout);
      va_end (ap);
    }
}

static void
write_to_message_stream (const char *format, ...)
{
  if (config->do_errors)
    {
      va_list ap;

      va_start (ap, format);
      vfprintf (stderr, format, ap);
      fflush (stderr);
      va_end (ap);
    }
}


/*
 * write_to_cw_sender()
 *
 * Fprintf-like function that allows us to conveniently print to the cw
 * output 'stream'.
 */
static void
write_to_cw_sender (const char *format, ...)
{
  va_list ap;
  char buffer[128];

  /*
   * Format the CW send buffer using vsnprintf.  Formatted strings longer than
   * the declared buffer will be silently truncated to the buffer length.
   */
  va_start (ap, format);
  vsnprintf (buffer, sizeof (buffer), format, ap);
  va_end (ap);

  /* Sound the buffer, and wait for the send to complete. */
  if (!cw_send_string (buffer))
    {
      perror ("cw_send_string");
      cw_flush_tone_queue ();
      abort ();
    }
  if (!cw_wait_for_tone_queue_critical (1))
    {
      perror ("cw_wait_for_tone_queue_critical");
      cw_flush_tone_queue ();
      abort ();
    }
}


/*---------------------------------------------------------------------*/
/*  Embedded commands handling                                         */
/*---------------------------------------------------------------------*/

/*
 * parse_stream_query()
 *
 * Handle a query received in the input stream.  The command escape character
 * and the query character have already been read and recognized.
 */
static void
parse_stream_query (FILE *stream)
{
  int c, value;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_QUERY, c);
      return;
    case CW_CMDV_FREQUENCY:
      value = cw_get_frequency ();
      break;
    case CW_CMDV_VOLUME:
      value = cw_get_volume ();
      break;
    case CW_CMDV_SPEED:
      value = cw_get_send_speed ();
      break;
    case CW_CMDV_GAP:
      value = cw_get_gap ();
      break;
    case CW_CMDV_WEIGHTING:
      value = cw_get_weighting ();
      break;
    case CW_CMDV_ECHO:
      value = config->do_echo;
      break;
    case CW_CMDV_ERRORS:
      value = config->do_errors;
      break;
    case CW_CMDV_COMMANDS:
      value = config->do_commands;
      break;
    case CW_CMDV_COMBINATIONS:
      value = config->do_combinations;
      break;
    case CW_CMDV_COMMENTS:
      value = config->do_comments;
      break;
    }

  /* Write the value obtained above to the message stream. */
  write_to_message_stream ("%c%c%d", CW_STATUS_OK, c, value);
}


/*
 * parse_stream_cwquery()
 *
 * Handle a cwquery received in the input stream.  The command escape
 * character and the cwquery character have already been read and recognized.
 */
static void
parse_stream_cwquery (FILE *stream)
{
  int c, value;
  const char *format;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_CWQUERY, c);
      return;
    case CW_CMDV_FREQUENCY:
      value = cw_get_frequency ();
      format = _("%d HZ ");
      break;
    case CW_CMDV_VOLUME:
      value = cw_get_volume ();
      format = _("%d PERCENT ");
      break;
    case CW_CMDV_SPEED:
      value = cw_get_send_speed ();
      format = _("%d WPM ");
      break;
    case CW_CMDV_GAP:
      value = cw_get_gap ();
      format = _("%d DOTS ");
      break;
    case CW_CMDV_WEIGHTING:
      value = cw_get_weighting ();
      format = _("%d PERCENT ");
      break;
    case CW_CMDV_ECHO:
      value = config->do_echo;
      format = _("ECHO %s ");
      break;
    case CW_CMDV_ERRORS:
      value = config->do_errors;
      format = _("ERRORS %s ");
      break;
    case CW_CMDV_COMMANDS:
      value = config->do_commands;
      format = _("COMMANDS %s ");
      break;
    case CW_CMDV_COMBINATIONS:
      value = config->do_combinations;
      format = _("COMBINATIONS %s ");
      break;
    case CW_CMDV_COMMENTS:
      value = config->do_comments;
      format = _("COMMENTS %s ");
      break;
    }

  switch (c)
    {
    case CW_CMDV_FREQUENCY:
    case CW_CMDV_VOLUME:
    case CW_CMDV_SPEED:
    case CW_CMDV_GAP:
    case CW_CMDV_WEIGHTING:
      write_to_cw_sender (format, value);
      break;
    case CW_CMDV_ECHO:
    case CW_CMDV_ERRORS:
    case CW_CMDV_COMMANDS:
    case CW_CMDV_COMBINATIONS:
    case CW_CMDV_COMMENTS:
      write_to_cw_sender (format, value ? _("ON") : _("OFF"));
      break;
    }
}


/*
 * parse_stream_parameter()
 *
 * Handle a parameter setting command received in the input stream.  The
 * command type character has already been read from the stream, and is passed
 * in as the first argument.
 */
static void
parse_stream_parameter (int c, FILE *stream)
{
  int value;
  int (*value_handler) (int);

  /* Parse and check the new parameter value. */
  if (fscanf (stream, "%d;", &value) != 1)
    {
      write_to_message_stream ("%c%c", CW_STATUS_ERR, c);
      return;
    }

  /* Either assign a handler, or update the local flag, as appropriate. */
  value_handler = NULL;
  switch (c)
    {
    case EOF:
    default:
      return;
    case CW_CMDV_FREQUENCY:
      value_handler = cw_set_frequency;
      break;
    case CW_CMDV_VOLUME:
      value_handler = cw_set_volume;
      break;
    case CW_CMDV_SPEED:
      value_handler = cw_set_send_speed;
      break;
    case CW_CMDV_GAP:
      value_handler = cw_set_gap;
      break;
    case CW_CMDV_WEIGHTING:
      value_handler = cw_set_weighting;
      break;
    case CW_CMDV_ECHO:
      config->do_echo = value;
      break;
    case CW_CMDV_ERRORS:
      config->do_errors = value;
      break;
    case CW_CMDV_COMMANDS:
      config->do_commands = value;
      break;
    case CW_CMDV_COMBINATIONS:
      config->do_combinations = value;
      break;
    case CW_CMDV_COMMENTS:
      config->do_comments = value;
      break;
    }

  /*
   * If not a local flag, apply the new value to a CW library control using
   * the handler assigned above.
   */
  if (value_handler)
    {
      if (!(*value_handler) (value))
        {
          write_to_message_stream ("%c%c", CW_STATUS_ERR, c);
          return;
        }
    }

  /* Confirm the new value with a stderr message. */
  write_to_message_stream ("%c%c%d", CW_STATUS_OK, c, value);
}


/*
 * parse_stream_command()
 *
 * Handle a command received in the input stream.  The command escape
 * character has already been read and recognized.
 */
static void
parse_stream_command (FILE *stream)
{
  int c;

  c = toupper (fgetc (stream));
  switch (c)
    {
    case EOF:
      return;
    default:
      write_to_message_stream ("%c%c%c", CW_STATUS_ERR, CW_CMD_ESCAPE, c);
      return;
    case CW_CMDV_FREQUENCY:
    case CW_CMDV_VOLUME:
    case CW_CMDV_SPEED:
    case CW_CMDV_GAP:
    case CW_CMDV_WEIGHTING:
    case CW_CMDV_ECHO:
    case CW_CMDV_ERRORS:
    case CW_CMDV_COMMANDS:
    case CW_CMDV_COMBINATIONS:
    case CW_CMDV_COMMENTS:
      parse_stream_parameter (c, stream);
      break;
    case CW_CMD_QUERY:
      parse_stream_query (stream);
      break;
    case CW_CMD_CWQUERY:
      parse_stream_cwquery (stream);
      break;
    case CW_CMDV_QUIT:
      cw_flush_tone_queue ();
      write_to_echo_stream ("%c", '\n');
      exit (EXIT_SUCCESS);
    }
}


/*---------------------------------------------------------------------*/
/*  Input stream handling                                              */
/*---------------------------------------------------------------------*/

/*
 * send_cw_character()
 *
 * Sends the given character to the CW sender, and waits for it to complete
 * sounding the tones.  The character to send may be a partial or a complete
 * character.
 */
static void
send_cw_character (int c, int is_partial)
{
  int character, status;

  /* Convert all whitespace into a single space. */
  character = isspace (c) ? ' ' : c;

  /* Send the character to the CW sender. */
  status = is_partial ? cw_send_character_partial (character)
                      : cw_send_character (character);
  if (!status)
    {
      if (errno != ENOENT)
        {
          perror ("cw_send_character[_partial]");
          cw_flush_tone_queue ();
          abort ();
        }
      else
        {
          write_to_message_stream ("%c%c", CW_STATUS_ERR, character);
          return;
        }
    }

  /* Echo the original character while sending it. */
  write_to_echo_stream ("%c", c);

  /* Wait for the character to complete. */
  if (!cw_wait_for_tone_queue_critical (1))
    {
      perror ("cw_wait_for_tone_queue_critical");
      cw_flush_tone_queue ();
      abort ();
    }
}


/*
 * parse_stream()
 *
 * Read characters from a file stream, and either sound them, or interpret
 * controls in them.  Returns on end of file.
 */
static void
parse_stream (FILE *stream)
{
  int c;
  enum { NONE, COMBINATION, COMMENT, NESTED_COMMENT } state = NONE;

  /*
   * Cycle round states depending on input characters.  Comments may be
   * nested inside combinations, but not the other way around; that is,
   * combination starts and ends are not special within comments.
   */
  for (c = fgetc (stream); !feof (stream); c = fgetc (stream))
    {
      switch (state)
        {
        case NONE:
          /*
           * Start a comment or combination, handle a command escape, or send
           * the character if none of these checks apply.
           */
          if (config->do_comments && c == CW_COMMENT_START)
            {
              state = COMMENT;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_combinations && c == CW_COMBINATION_START)
            {
              state = COMBINATION;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_commands && c == CW_CMD_ESCAPE)
            parse_stream_command (stream);
          else
            send_cw_character (c, false);
          break;

        case COMBINATION:
          /*
           * Start a comment nested in a combination, end a combination,
           * handle a command escape, or send the character if none of these
           * checks apply.
           */
          if (config->do_comments && c == CW_COMMENT_START)
            {
              state = NESTED_COMMENT;
              write_to_echo_stream ("%c", c);
            }
          else if (c == CW_COMBINATION_END)
            {
              state = NONE;
              write_to_echo_stream ("%c", c);
            }
          else if (config->do_commands && c == CW_CMD_ESCAPE)
            parse_stream_command (stream);
          else
            {
              /*
               * If this is the final character in the combination, do not
               * suppress the end of character delay.  To do this, look ahead
               * the next character, and suppress unless combination end.
               */
              int lookahead;

              lookahead = fgetc (stream);
              ungetc (lookahead, stream);
              send_cw_character (c, lookahead != CW_COMBINATION_END);
            }
          break;

        case COMMENT:
        case NESTED_COMMENT:
          /*
           * If in a comment nested in a combination and comment end seen,
           * revert state to reflect in combination only.  If in an unnested
           * comment and comment end seen, reset state.
           */
          if (c == CW_COMMENT_END)
            state = (state == NESTED_COMMENT) ? COMBINATION : NONE;
          write_to_echo_stream ("%c", c);
          break;
        }
    }
}





/**
   \brief Parse command line args, then produce CW output until end of file

   \param argc
   \param argv
*/
int main (int argc, char *const argv[])
{
	atexit(cw_atexit);

	/* Set locale and message catalogs. */
	i18n_initialize();

	/* Parse combined environment and command line arguments. */
	int combined_argc;
	char **combined_argv;
	combine_arguments("CW_OPTIONS", argc, argv, &combined_argc, &combined_argv);

	config = cw_config_new(cw_program_basename(argv[0]));
	if (!config) {
		return EXIT_FAILURE;
	}
	config->is_cw = 1;

	if (!cw_process_argv(argc, argv, all_options, config)) {
		fprintf(stderr, _("%s: failed to parse command line args\n"), config->program_name);
		return EXIT_FAILURE;
	}
	if (!cw_config_is_valid(config)) {
		fprintf(stderr, _("%s: inconsistent command line arguments\n"), config->program_name);
		return EXIT_FAILURE;
	}

	if (config->input_file) {
		if (!freopen(config->input_file, "r", stdin)) {
			fprintf(stderr, _("%s: %s\n"), config->program_name, strerror(errno));
			fprintf(stderr, _("%s: error opening input file %s\n"), config->program_name, config->input_file);
			return EXIT_FAILURE;
		}
	}

	if (config->audio_system == CW_AUDIO_ALSA
	    && cw_is_pa_possible(NULL)) {

		fprintf(stdout, "Selected audio system is ALSA, but audio on your system is handled by PulseAudio. Expect problems with timing.\n");
		fprintf(stdout, "In this situation it is recommended to run %s like this:\n", config->program_name);
		fprintf(stdout, "%s -s p\n\n", config->program_name);
		fprintf(stdout, "Press Enter key to continue\n");
		getchar();
	}

	generator = cw_generator_new_from_config(config);
	if (!generator) {
		//fprintf(stderr, "%s: failed to create generator with device '%s'\n", config->program_name, config->audio_device);
		return EXIT_FAILURE;
	}

	/* Set up signal handlers to exit on a range of signals. */
	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	for (int i = 0; SIGNALS[i]; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], SIG_DFL)) {
			fprintf(stderr, _("%s: can't register signal: %s\n"), config->program_name, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	/* Start producing sine wave (amplitude of the wave will be
	   zero as long as there are no characters to process). */
	cw_generator_start();

	/* Send stdin stream to CW parsing. */
	parse_stream(stdin);

	/* Await final tone completion before exiting. */
	cw_wait_for_tone_queue();

	return EXIT_SUCCESS;
}





void cw_atexit(void)
{
	if (generator) {
		cw_generator_stop();
		//cw_complete_reset();
		cw_generator_delete();
	}

	if (config) {
		cw_config_delete(&config);
	}

	return;
}
