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

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <curses.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"
#include "i18n.h"
#include "cmdline.h"
#include "cw_copyright.h"
#include "dictionary.h"
#include "memory.h"





/*---------------------------------------------------------------------*/
/*  Module variables, miscellaneous other stuff                        */
/*---------------------------------------------------------------------*/

/* Flag set if colors are requested on the user interface. */
static bool do_colors = true;

/* Are we at the beginning of buffer displaying played characters? */
static bool beginning_of_buffer = true;
/* Current sending state, active or idle. */
static bool is_sending_active = false;


/* Width of parameter windows, displayed at the bottom of window.
   Since the same value is used as size of char buffer, make one
   character for terminating NUL explicit as +1.
 */
#define CWCP_PARAM_WIDTH (15 + 1)


static cw_config_t *config = NULL; /* program-specific configuration */
static bool generator = false;     /* have we created a generator? */
static const char *all_options = "s:|system,d:|device,"
	"w:|wpm,t:|tone,v:|volume,"
	"g:|gap,k:|weighting,"
	"f:|infile,F:|outfile,"
	"T:|time,"
	/* "c:|colours,c:|colors,m|mono," */
	"h|help,V|version";



static WINDOW *screen  = NULL,
	*text_window   = NULL, *text_subwindow   = NULL,
	*mode_window   = NULL, *mode_subwindow   = NULL,
	*speed_window  = NULL, *speed_subwindow  = NULL,
	*tone_window   = NULL, *tone_subwindow   = NULL,
	*volume_window = NULL, *volume_subwindow = NULL,
	*gap_window    = NULL, *gap_subwindow    = NULL,
	*timer_window  = NULL, *timer_subwindow  = NULL;

static void cwcp_atexit(void);

static int  timer_get_total_practice_time(void);
static bool timer_set_total_practice_time(int practice_time);
static void timer_start(void);
static bool timer_is_expired(void);
static void timer_window_update(int elapsed, int total);

static void speed_update(void);
static void frequency_update(void);
static void volume_update(void);
static void gap_update(void);


typedef enum { M_DICTIONARY, M_KEYBOARD, M_EXIT } mode_type_t;

static void mode_initialize(void);
static void mode_clean(void);
static bool mode_change_to_next(void);
static bool mode_change_to_previous(void);
static int  mode_get_current(void);
static int  mode_get_count(void);
static const char *mode_get_description(int index);
static bool mode_current_is_type(mode_type_t type);
static bool mode_is_sending_active(void);



/* Definition of an interface operating mode; its description, related
   dictionary, and data on how to send for the mode. */
struct mode_s {
	const char *description;       /* Text mode description */
	mode_type_t type;              /* Mode type; {M_DICTIONARY|M_KEYBOARD|M_EXIT} */
	const cw_dictionary_t *dict;   /* Dictionary, if type is dictionary */
};


typedef struct mode_s *moderef_t;

/* Modes table, current program mode, and count of modes in the table.
   The program is always in one of these modes, indicated by
   current_mode. */
static moderef_t modes = NULL,
                 current_mode = NULL;
static int modes_count = 0;

static int queue_get_length(void);
static int queue_next_index(int index);
static int queue_prior_index(int index);
static void queue_display_add_character(void);
static void queue_display_delete_character(void);
static void queue_display_highlight_character(bool is_highlight);
static void queue_discard_contents(void);
static void queue_dequeue_character(void);
static void queue_enqueue_string(const char *word);
static void queue_enqueue_character(char c);
static void queue_enqueue_random_dictionary_text(moderef_t mode, bool beginning_of_buffer);
static void queue_transfer_character_to_libcw(void);
static void queue_delete_character(void);

static void ui_refresh_main_window(void);
static void ui_display_state(const char *state);
static void ui_clear_main_window(void);
static void ui_poll_user_input(int fd, int usecs);
static void ui_update_mode_selection(int old_mode, int current_mode);
static void ui_handle_event(int c);

static void    ui_destroy(void);
static void    ui_initialize(void);
static WINDOW *ui_init_window(int lines, int columns, int begin_y, int begin_x, const char *header);
static void    ui_init_display(int lines, int columns, int begin_y, int begin_x, const char *header, WINDOW **window, WINDOW **subwindow);
static WINDOW *ui_init_screen(void);

static void signal_handler(int signal_number);

static void state_change_to_active(void);
static void state_change_to_idle(void);





/*---------------------------------------------------------------------*/
/*  Circular character queue                                           */
/*---------------------------------------------------------------------*/

/* Characters awaiting send are stored in a circular buffer,
   implemented as an array with tail and head indexes that wrap. */
enum { QUEUE_CAPACITY = 256 };
static volatile char queue_data[QUEUE_CAPACITY];
static volatile int queue_tail = 0,
                    queue_head = 0;

/* There are times where we have no data to send.  For these cases,
   record as idle, so that we know when to wake the sender. */
static volatile bool is_queue_idle = true;




/**
   \brief Return the count of characters currently held in the circular buffer
*/
int queue_get_length(void)
{
	return queue_tail >= queue_head
		? queue_tail - queue_head : queue_tail - queue_head + QUEUE_CAPACITY;
}





/**
   \brief Advance a tone queue index, including circular wrapping
*/
int queue_next_index(int index)
{
	return (index + 1) % QUEUE_CAPACITY;
}





/**
   \brief Regress a tone queue index, including circular wrapping
*/
int queue_prior_index(int index)
{
	return index == 0 ? QUEUE_CAPACITY - 1 : index - 1;
}





/**
   Add a character to the text display when queueing
*/
void queue_display_add_character(void)
{
	/* Append the last queued character to the text display. */
	if (queue_get_length() > 0) {
		waddch(text_subwindow, toupper(queue_data[queue_tail]));
		wrefresh(text_subwindow);
	}

	return;
}





/**
   Delete a character to the text display when dequeueing
*/
void queue_display_delete_character(void)
{
	int y, x, max_x;
	__attribute__((unused)) int max_y;

	/* Get the text display dimensions and current coordinates. */
	getmaxyx(text_subwindow, max_y, max_x);
	getyx(text_subwindow, y, x);

	/* Back the cursor up one position. */
	x--;
	if (x < 0) {
		x += max_x;
		y--;
	}

	/* If these coordinates are on screen, write a space and back up. */
	if (y >= 0) {
		wmove(text_subwindow, y, x);
		waddch(text_subwindow, ' ');
		wmove(text_subwindow, y, x);
		wrefresh(text_subwindow);
	}

	return;
}





/**
   Highlight or un-highlight a character in the text display when dequeueing
*/
void queue_display_highlight_character(bool is_highlight)
{
	int y, x, max_x;
	__attribute__((unused)) int max_y;

	/* Get the text display dimensions and current coordinates. */
	getmaxyx(text_subwindow, max_y, max_x);
	getyx(text_subwindow, y, x);

	/* Find the coordinates for the queue head character. */
	x -= queue_get_length() + 1;
	while (x < 0) {
		x += max_x;
		y--;
	}

	/* If these coordinates are on screen, highlight or
	   unhighlight, and then restore the cursor position so that
	   it remains unchanged. */
	if (y >= 0) {
		int saved_y, saved_x;

		getyx(text_subwindow, saved_y, saved_x);
		wmove(text_subwindow, y, x);
		waddch(text_subwindow,
		       is_highlight ? winch(text_subwindow) | A_REVERSE
			: winch(text_subwindow) & ~A_REVERSE);
		wmove(text_subwindow, saved_y, saved_x);
		wrefresh(text_subwindow);
	}

	return;
}





/**
   \brief Forcibly empty the queue, if not already idle
*/
void queue_discard_contents(void)
{
	if (!is_queue_idle) {
		queue_display_highlight_character(false);
		queue_head = queue_tail;
		is_queue_idle = true;
	}

	return;
}





/**
   \brief Dequeue a character

   Called when the CW send buffer is empty.  If the queue is not idle,
   take the next character from the queue and send it.  If there are
   no more queued characters, set the queue to idle.
*/
void queue_dequeue_character(void)
{
	if (!is_queue_idle) {
		/* Unhighlight any previous highlighting, and see if
		   we can dequeue. */
		queue_display_highlight_character(false);
		if (queue_get_length() > 0) {
			char c;

			/* Take the next character off the queue,
			   highlight, and send it.  We don't expect
			   sending to fail because only sendable
			   characters are queued. */
			queue_head = queue_next_index(queue_head);
			c = queue_data[queue_head];
			queue_display_highlight_character(true);

			if (!cw_send_character(c)) {
				perror("cw_send_character");
				abort();
			}
		} else {
			is_queue_idle = true;
		}
	}

	return;
}





/**
   \brief Queue a string for sending by the CW sender

   Function rejects any unsendable character, and also any characters
   passed in where the character queue is already full.  Rejection is
   silent.

   \param word - string to send
*/
void queue_enqueue_string(const char *word)
{
	bool is_queue_notify = false;
	for (int i = 0; word[i] != '\0'; i++) {

		char c = toupper(word[i]);
		if (cw_character_is_valid(c)) {
			/* Calculate the new character queue tail.  If
			   the new value will not hit the current
			   queue head, add the character to the
			   queue. */
			if (queue_next_index(queue_tail) != queue_head) {
				queue_tail = queue_next_index(queue_tail);
				queue_data[queue_tail] = c;
				queue_display_add_character();

				if (is_queue_idle) {
					is_queue_notify = true;
				}
			}
		}
	}

	/* If we queued any character, mark the queue as not idle. */
	if (is_queue_notify) {
		is_queue_idle = false;
	}

	return;
}





/**
   \brief Queue a character for sending by the CW sender

   Function rejects any unsendable character, and also any characters
   passed in where the character queue is already full.  Rejection is
   silent.

   \param c - character to send
*/
void queue_enqueue_character(char c)
{
	char buffer[2];

	buffer[0] = c;
	buffer[1] = '\0';
	queue_enqueue_string(buffer);

	return;
}





/**
   \brief Delete the most recently added character from the queue

   Remove the most recently added character from the queue, provided
   that the dequeue hasn't yet reached it.  If there's nothing
   available to delete, fail silently.
*/
void queue_delete_character(void)
{
	/* If data is queued, regress tail and delete one display character. */
	if (queue_get_length() > 0) {
		queue_tail = queue_prior_index(queue_tail);
		queue_display_delete_character();
	}

	return;
}





/**
   Add a group of elements from current dictionary to cwcp's character queue

   Function adds a group of letters, or a word, to cwcp's character
   queue. The group or the word is then played and displayed in main
   window.

   The function also enqueues space separating words/groups if \p
   beginning is true. You want to pass true only for first call of the
   function for given mode (only when the program should queue and
   play first group/word).

   \param mode - mode determining from which dictionary to get elements
   \param beginning - are we at the beginning of buffer/window?
*/
void queue_enqueue_random_dictionary_text(moderef_t mode, bool beginning)
{
	if (!beginning) {
		queue_enqueue_character(' ');
	}

	/* Size of group of letters that will be printed together
	   to main window of cwcp. '1' for dictionaries consisting
	   of multi-character words (so you get single words separated
	   with spaces), or '5' for single-character words (so you get
	   5-letter chunks separated with spaces). */
	int group_size = cw_dictionary_get_group_size(mode->dict);

	/* Select and buffer N random elements selected from dictionary. */
	for (int group = 0; group < group_size; group++) {
		/* For dictionaries with size of word in dictionary == 1
		   this returns single letters. */
		queue_enqueue_string(cw_dictionary_get_random_word(mode->dict));
	}

	return;
}





/**
   Check the libcw's tone queue, and if it is getting low, arrange for
   more data to be passed in to the libcw's tone queue.
*/
void queue_transfer_character_to_libcw(void)
{
	if (cw_get_tone_queue_length() > 1) {
		return;
	}

	if (!is_sending_active) {
		return;
	}

	/* Arrange more data for libcw.  The source for this data is
	   dependent on the mode.  If in dictionary modes, update and
	   check the timer, then add more random data if the queue is
	   empty.  If in keyboard mode, just dequeue anything
	   currently on the character queue. */

	if (current_mode->type == M_DICTIONARY) {
		if (timer_is_expired()) {
			state_change_to_idle();
			return;
		}

		if (queue_get_length() == 0) {
			queue_enqueue_random_dictionary_text(current_mode, beginning_of_buffer);
			if (beginning_of_buffer) {
				beginning_of_buffer = false;
			}
		}
	}

	if (current_mode->type == M_DICTIONARY
	    || current_mode->type == M_KEYBOARD) {

		queue_dequeue_character();
	}

	return;
}





/*---------------------------------------------------------------------*/
/*  Practice timer                                                     */
/*---------------------------------------------------------------------*/

static const int TIMER_MIN_TIME = 1, TIMER_MAX_TIME = 99; /* practice timer limits */
static int timer_total_practice_time = 15; /* total time of practice, from beginning to end */
static int timer_practice_start = 0;       /* time() value on practice start */





/**
   \brief Get current value of total time of practice
*/
int timer_get_total_practice_time(void)
{
	return timer_total_practice_time;
}





/**
   \brief Set total practice time

   Set total time (total duration) of practice.

   \param practice_time - new value of total practice time

   \return true on success
   \return false on failure
*/
bool timer_set_total_practice_time(int practice_time)
{
	if (practice_time >= TIMER_MIN_TIME && practice_time <= TIMER_MAX_TIME) {
		timer_total_practice_time = practice_time;
		return true;
	} else {
		return false;
	}
}





/**
   \brief Set the timer practice start time to the current time
*/
void timer_start(void)
{
	timer_practice_start = time(NULL);
	return;
}





/**
   \brief Update the practice timer, and return true if the timer expires

   \return true if timer has expired
   \return false if timer has not expired yet
*/
bool timer_is_expired(void)
{
	/* Update the display of minutes practiced. */
	int elapsed = (time(NULL) - timer_practice_start) / 60;
	timer_window_update(elapsed, timer_total_practice_time);

	/* Check the time, requesting stop if over practice time. */
	return elapsed >= timer_total_practice_time;
}





/**
   \brief Update value of time spent on practicing

   Function updates 'timer display' with one or two values of practice
   time: time elapsed and time total. Both times are in minutes.

   You can pass a negative value of \p elapsed - function will use
   previous valid value (which is zero at first time).

   \param elapsed - time elapsed from beginning of practice
   \param total - total time of practice
*/
void timer_window_update(int elapsed, int total)
{
	static int el = 0;
	if (elapsed >= 0) {
		el = elapsed;
	}

	char buffer[CWCP_PARAM_WIDTH];
	snprintf(buffer, CWCP_PARAM_WIDTH, total == 1 ? _("%2d/%2d min ") : _("%2d/%2d mins"), el, total);
	mvwaddstr(timer_subwindow, 0, 2, buffer);
	wrefresh(timer_subwindow);

	return;
}





/*---------------------------------------------------------------------*/
/*  General program state and mode control                             */
/*---------------------------------------------------------------------*/





/**
   \brief Initialize modes

   Build up the modes from the known dictionaries, then add non-dictionary
   modes.
*/
void mode_initialize(void)
{
	if (modes) {
		/* Dispose of any pre-existing modes -- unlikely. */
		free(modes);
		modes = NULL;
	}

	/* Start the modes with the known dictionaries. */
	int count = 0;
	for (const cw_dictionary_t *dict = cw_dictionaries_iterate(NULL);
	     dict;
	     dict = cw_dictionaries_iterate(dict)) {

		modes = safe_realloc(modes, sizeof (*modes) * (count + 1));
		modes[count].description = cw_dictionary_get_description(dict);
		modes[count].type = M_DICTIONARY;
		modes[count++].dict = dict;
	}

	/* Add keyboard, exit, and null sentinel. */
	modes = safe_realloc(modes, sizeof (*modes) * (count + 3));
	modes[count].description = _("Keyboard");
	modes[count].type = M_KEYBOARD;
	modes[count++].dict = NULL;

	modes[count].description = _("Exit (F12)");
	modes[count].type = M_EXIT;
	modes[count++].dict = NULL;

	memset(modes + count, 0, sizeof (*modes));

	/* Initialize the current mode to be the first listed, and set count. */
	current_mode = modes;
	modes_count = count;

	return;
}





/**
   \brief Free data structures relates to modes

   Call this function at exit
*/
void mode_clean(void)
{
	free(modes);
	modes = NULL;

	return;
}





/**
   \brief Get count of modes
*/
int mode_get_count(void)
{
	return modes_count;
}





/**
   \brief Get index of the current mode
*/
int mode_get_current(void)
{
	return current_mode - modes;
}





/**
   \brief Get description of a mode at given index
*/
const char *mode_get_description(int index)
{
	return modes[index].description;
}





/**
   \brief Get result of a type comparison for the current mode
*/
bool mode_current_is_type(mode_type_t type)
{
	return current_mode->type == type;
}





/**
   \brief Change the mode to next one

   Advance the current node, returning false if at the limit.

   \return true if mode has been changed
   \return false if mode has not been changed because it was the last on list
*/
bool mode_change_to_next(void)
{
	current_mode++;
	if (!current_mode->description) {
		current_mode--;
		return false;
	} else {
		return true;
	}
}





/**
   \brief Change the mode to previous one

   Regress the current node, returning false if at the limit.

   \return true if mode has been changed
   \return false if mode has not been changed because it was the first on list
*/
bool mode_change_to_previous(void)
{
	if (current_mode > modes) {
		current_mode--;
		return true;
	} else {
		return false;
	}
}





/**
   \brief Change the state of the program from idle to actively sending.
*/
void state_change_to_active(void)
{
	static moderef_t last_mode = NULL;  /* Detect changes of mode */

	if (is_sending_active) {
		return;
	}

	cw_start_beep();

	is_sending_active = true;

	ui_display_state(_("Sending(F9 or Esc to exit)"));

	if (current_mode != last_mode) {
		ui_clear_main_window();
		timer_start();

		/* Don't allow a space at the beginning of buffer. */
		beginning_of_buffer = true;

		last_mode = current_mode;
        }

	ui_refresh_main_window();

	return;
}





/**
   Change the state of the program from actively sending to idle.
*/
void state_change_to_idle(void)
{
	if (!is_sending_active) {
		return;
	}

	is_sending_active = false;

	ui_display_state(_("Start(F9)"));
	touchwin(text_subwindow);
	wnoutrefresh(text_subwindow);
	doupdate();

	/* Remove everything in the outgoing character queue. */
	queue_discard_contents();

	cw_end_beep();

	return;
}





/**
   \brief Check if sending is active

   \return true if currently sending
   \return false otherwise
*/
bool mode_is_sending_active(void)
{
	return is_sending_active;
}





/*---------------------------------------------------------------------*/
/*  User interface initialization and event handling                   */
/*---------------------------------------------------------------------*/

/*
 * User interface introduction strings, split in two to avoid the 509
 * character limit imposed by ISO C89 on string literal lengths.
 */
static const char *const INTRODUCTION = N_(
  "UNIX/Linux Morse Tutor v3.4.2\n"
  "Copyright (C) 1997-2006 Simon Baldwin\n"
  "Copyright (C) 2011-2015 Kamil Ignacak\n"
  "---------------------------------------------------------\n"
  "Cwcp is an interactive Morse code tutor program, designed\n"
  "both for learning Morse code for the first time, and for\n"
  "experienced Morse users who want, or need, to improve\n"
  "their receiving speed.\n");
static const char *const INTRODUCTION_CONTINUED = N_(
  "---------------------------------------------------------\n"
  "Select mode:                   Up/Down arrow/F10/F11\n"
  "Start sending selected mode:   Enter/F9\n"
  "Pause:                         F9/Esc\n"
  "Resume:                        F9\n"
  "Exit program:                  menu->Exit/F12/^C\n"
  "Use keys specified below to adjust speed, tone, volume,\n"
  "and spacing of the Morse code at any time.\n");

/* Alternative F-keys for folks without (some, or all) F-keys. */
enum
{ CTRL_OFFSET = 0100,                   /* Ctrl keys are 'X' - 0100 */
  PSEUDO_KEYF1 = 'Q' - CTRL_OFFSET,     /* Alternative FKEY1 */
  PSEUDO_KEYF2 = 'W' - CTRL_OFFSET,     /* Alternative FKEY2 */
  PSEUDO_KEYF3 = 'E' - CTRL_OFFSET,     /* Alternative FKEY3 */
  PSEUDO_KEYF4 = 'R' - CTRL_OFFSET,     /* Alternative FKEY4 */
  PSEUDO_KEYF5 = 'T' - CTRL_OFFSET,     /* Alternative FKEY5 */
  PSEUDO_KEYF6 = 'Y' - CTRL_OFFSET,     /* Alternative FKEY6 */
  PSEUDO_KEYF7 = 'U' - CTRL_OFFSET,     /* Alternative FKEY7 */
  PSEUDO_KEYF8 = 'I' - CTRL_OFFSET,     /* Alternative FKEY8 */
  PSEUDO_KEYF9 = 'A' - CTRL_OFFSET,     /* Alternative FKEY9 */
  PSEUDO_KEYF10 = 'S' - CTRL_OFFSET,    /* Alternative FKEY10 */
  PSEUDO_KEYF11 = 'D' - CTRL_OFFSET,    /* Alternative FKEY11 */
  PSEUDO_KEYF12 = 'F' - CTRL_OFFSET,    /* Alternative FKEY12 */
  PSEUDO_KEYNPAGE = 'O' - CTRL_OFFSET,  /* Alternative PageDown */
  PSEUDO_KEYPPAGE = 'P' - CTRL_OFFSET   /* Alternative PageUp */
};

/* User interface event loop running flag. */
static bool is_running = true;

/* Color definitions. */
static const short color_array[] = {
  COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
  COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
};
enum { COLORS_COUNT = sizeof (color_array) / sizeof (color_array[0]) };

enum {
	DISPLAY_EXTERNAL_COLORS = 1,   /* Normal color pair */
	DISPLAY_INTERNAL_COLORS = 2,   /* Blue color pair */
	DISPLAY_FOREGROUND      = 7,   /* White foreground */
	DISPLAY_BACKGROUND      = 4,   /* Blue background */
	BOX_FOREGROUND          = 7,   /* White foreground */
	BOX_BACKGROUND          = 0    /* Black background */
};

/* Color values as arrays into color_array. */
static int display_foreground = DISPLAY_FOREGROUND,  /* White foreground */
           display_background = DISPLAY_BACKGROUND,  /* Blue background */
           box_foreground = BOX_FOREGROUND,          /* White foreground */
           box_background = BOX_BACKGROUND;          /* Black background */





/**
   \brief Initialize main window
*/
WINDOW *ui_init_screen(void)
{
	/* Create the main window for the complete screen. */
	WINDOW *window = initscr();
	wrefresh(window);

	/* If using colors, set up a base color for the screen. */
	if (do_colors && has_colors())	{
		start_color();

		init_pair(DISPLAY_EXTERNAL_COLORS,
			  color_array[box_foreground],
			  color_array[box_background]);

		init_pair(DISPLAY_INTERNAL_COLORS,
			  color_array[display_foreground],
			  color_array[display_background]);

		wbkgdset(window, COLOR_PAIR (DISPLAY_EXTERNAL_COLORS) | ' ');
		werase(window);
		wrefresh(window);
	}

	return window;
}





/**
   \brief Create new window

   Create new window with initial header and contents.
   The function allocates new ncurses WINDOW.

   \return new window
*/
WINDOW *ui_init_window(int lines, int columns, int begin_y, int begin_x, const char *header)
{
	/* Create the window, and set up colors if possible and requested. */
	WINDOW *window = newwin(lines, columns, begin_y, begin_x);
	if (!window) {
		fprintf(stderr, "newwin()\n");
		exit(EXIT_FAILURE);
	}

	if (do_colors && has_colors()) {
		wbkgdset(window, COLOR_PAIR (DISPLAY_EXTERNAL_COLORS) | ' ');
		wattron(window, COLOR_PAIR (DISPLAY_EXTERNAL_COLORS));
		werase(window);
	}

	/* Test on top of the area. */
	box(window, 0, 0);
	mvwaddstr(window, 0, 1, header);

	wrefresh(window);
	return window;
}





/**
   \brief Create new main window for Morse code text

   Function allocates two new ncurses WINDOW variables.
*/
void ui_init_display(int lines, int columns, int begin_y, int begin_x,
			  const char *header,
			  WINDOW **window, WINDOW **subwindow)
{
	*window = ui_init_window(lines, columns, begin_y, begin_x, header);


	/* Text subwindow: Create the window, and set up colors if possible and requested. */
	*subwindow = newwin(lines - 2, columns - 2, begin_y + 1, begin_x + 1);
	if (!*subwindow) {
		fprintf(stderr, "newwin()\n");
		exit(EXIT_FAILURE);
	}

	if (do_colors && has_colors()) {
		wbkgdset(*subwindow, COLOR_PAIR (DISPLAY_INTERNAL_COLORS) | ' ');
		wattron(*subwindow, COLOR_PAIR (DISPLAY_INTERNAL_COLORS));
		werase(*subwindow);
	}

	wrefresh(*subwindow);

	return;
}





/**
   \brief Initialize the user interface, boxes and windows
*/
void ui_initialize(void)
{
	static bool is_initialized = false;

	int max_y, max_x;

	/* Create the over-arching screen window. */
	screen = ui_init_screen();
	getmaxyx(screen, max_y, max_x);

	/* Create and box in the mode window. */
	ui_init_display(max_y - 3, 20, 0, 0, _("Mode(F10v,F11^)"), &mode_window, &mode_subwindow);

	for (int i = 0; i < mode_get_count(); i++) {
		if (i == mode_get_current()) {
			wattron(mode_subwindow, A_REVERSE);
		} else {
			wattroff(mode_subwindow, A_REVERSE);
		}
		mvwaddstr(mode_subwindow, i, 1, mode_get_description(i));
	}
	wrefresh(mode_subwindow);

	/* Create the text display window; do the introduction only once. */
	ui_init_display(max_y - 3, max_x - 20, 0, 20, _("Start(F9)"), &text_window, &text_subwindow);
	wmove(text_subwindow, 0, 0);
	if (!is_initialized) {
		waddstr(text_subwindow, _(INTRODUCTION));
		waddstr(text_subwindow, _(INTRODUCTION_CONTINUED));
		is_initialized = true;
	}
	wrefresh(text_subwindow);
	idlok(text_subwindow, true);
	immedok(text_subwindow, true);
	scrollok(text_subwindow, true);

	int lines = 3;
	int columns = CWCP_PARAM_WIDTH;

	/* Create the control feedback boxes. */

	ui_init_display(lines, columns, max_y - lines, columns * 0, _("Speed(F1-,F2+)"), &speed_window, &speed_subwindow);
	speed_update();

	ui_init_display(lines, columns, max_y - lines, columns * 1, _("Tone(F3-,F4+)"), &tone_window, &tone_subwindow);
	frequency_update();

	ui_init_display(lines, columns, max_y - lines, columns * 2, _("Vol(F5-,F6+)"), &volume_window, &volume_subwindow);
	volume_update();

	ui_init_display(lines, columns, max_y - lines, columns * 3, _("Gap(F7-,F8+)"), &gap_window, &gap_subwindow);
	gap_update();

	ui_init_display(lines, columns, max_y - lines, columns * 4, _("Time(Dn-,Up+)"), &timer_window, &timer_subwindow);
	timer_window_update(0, timer_get_total_practice_time());

	/* Set up curses input mode. */
	keypad(screen, true);
	noecho();
	cbreak();
	curs_set(0);
	raw();
	nodelay(screen, false);

	wrefresh(curscr);

	return;
}





/**
   \brief Dismantle the user interface, boxes and windows
*/
void ui_destroy(void)
{
	if (text_subwindow) {
		delwin(text_subwindow);
		text_subwindow = NULL;
	}
	if (text_window) {
		delwin(text_window);
		text_window = NULL;
	}

	if (mode_subwindow) {
		delwin(mode_subwindow);
		mode_subwindow = NULL;
	}
	if (mode_window) {
		delwin(mode_window);
		mode_window = NULL;
	}

	if (speed_subwindow) {
		delwin(speed_subwindow);
		speed_subwindow = NULL;
	}
	if (speed_window) {
		delwin(speed_window);
		speed_window = NULL;
	}

	if (tone_subwindow) {
		delwin(tone_subwindow);
		tone_subwindow = NULL;
	}
	if (tone_window) {
		delwin(tone_window);
		tone_window = NULL;
	}

	if (volume_subwindow) {
		delwin(volume_subwindow);
		volume_subwindow = NULL;
	}
	if (volume_window) {
		delwin(volume_window);
		volume_window = NULL;
	}

	if (gap_subwindow) {
		delwin(gap_subwindow);
		gap_subwindow = NULL;
	}
	if (gap_window) {
		delwin(gap_window);
		gap_window = NULL;
	}

	if (timer_subwindow) {
		delwin(timer_subwindow);
		timer_subwindow = NULL;
	}
	if (timer_window) {
		delwin(timer_window);
		timer_window = NULL;
	}

	if (screen) {
		/* Clear the screen for neatness. */
		werase(screen);
		wrefresh(screen);

		delwin(screen);
		screen = NULL;
	}

	/* End curses processing. */
	endwin();

	return;
}





/**
   \brief Assess a user command, and action it if valid

   If the command turned out to be a valid user interface command,
   return true, otherwise return false.
*/
static int interface_interpret(int c)
{
	/* Interpret the command passed in */
	switch (c) {
	default:
		return false;

	case ']':
		display_background = (display_background + 1) % COLORS_COUNT;
		goto color_update;

	case '[':
		display_foreground = (display_foreground + 1) % COLORS_COUNT;
		goto color_update;

	case '{':
		box_background = (box_background + 1) % COLORS_COUNT;
		goto color_update;

	case '}':
		box_foreground = (box_foreground + 1) % COLORS_COUNT;
		goto color_update;

	color_update:
		if (do_colors && has_colors()) {
			init_pair(DISPLAY_EXTERNAL_COLORS,
				  color_array[box_foreground],
				  color_array[box_background]);
			init_pair(DISPLAY_INTERNAL_COLORS,
				  color_array[display_foreground],
				  color_array[display_background]);
			wrefresh(curscr);
		}
		break;


	case 'L' - CTRL_OFFSET:
		wrefresh(curscr);
		break;


	case KEY_F (1):
	case PSEUDO_KEYF1:
	case KEY_LEFT:
		if (cw_set_send_speed(cw_get_send_speed() - CW_SPEED_STEP)) {
			speed_update();
		}
		break;

	case KEY_F (2):
	case PSEUDO_KEYF2:
	case KEY_RIGHT:
		if (cw_set_send_speed(cw_get_send_speed() + CW_SPEED_STEP)) {
			speed_update();
		}
		break;


	case KEY_F (3):
	case PSEUDO_KEYF3:
	case KEY_END:
		if (cw_set_frequency(cw_get_frequency() - CW_FREQUENCY_STEP)) {
			frequency_update();
		}
		break;

	case KEY_F (4):
	case PSEUDO_KEYF4:
	case KEY_HOME:
		if (cw_set_frequency(cw_get_frequency() + CW_FREQUENCY_STEP)) {
			frequency_update();
		}
		break;

	case KEY_F (5):
	case PSEUDO_KEYF5:
		if (cw_set_volume(cw_get_volume() - CW_VOLUME_STEP)) {
			volume_update();
		}
		break;

	case KEY_F (6):
	case PSEUDO_KEYF6:
		if (cw_set_volume(cw_get_volume() + CW_VOLUME_STEP)) {
			volume_update();
		}
		break;



	case KEY_F (7):
	case PSEUDO_KEYF7:
		if (cw_set_gap(cw_get_gap() - CW_GAP_STEP)) {
			gap_update();
		}
		break;

	case KEY_F (8):
	case PSEUDO_KEYF8:
		if (cw_set_gap(cw_get_gap() + CW_GAP_STEP)) {
			gap_update();
		}
		break;

	case KEY_NPAGE:
	case PSEUDO_KEYNPAGE:
		if (timer_set_total_practice_time(timer_get_total_practice_time() - CW_PRACTICE_TIME_STEP)) {
			timer_window_update(-1, timer_get_total_practice_time());
		}
		break;

	case KEY_PPAGE:
	case PSEUDO_KEYPPAGE:
		if (timer_set_total_practice_time(timer_get_total_practice_time() + CW_PRACTICE_TIME_STEP)) {
			timer_window_update(-1, timer_get_total_practice_time());
		}
		break;

	case KEY_F (11):
	case PSEUDO_KEYF11:
	case KEY_UP:
		{
			state_change_to_idle();
			int old_mode = mode_get_current();
			if (mode_change_to_previous()) {
				ui_update_mode_selection(old_mode, mode_get_current());
			}
		}
		break;

	case KEY_F (10):
	case PSEUDO_KEYF10:
	case KEY_DOWN:
		{
			state_change_to_idle();
			int old_mode = mode_get_current();
			if (mode_change_to_next()) {
				ui_update_mode_selection(old_mode, mode_get_current());
			}
		}
		break;

	case KEY_F (9):
	case PSEUDO_KEYF9:
	case '\n':
		if (mode_current_is_type(M_EXIT)) {
			is_running = false;
		} else {
			if (!mode_is_sending_active()) {
				state_change_to_active();
			} else {
				if (c != '\n') {
					state_change_to_idle();
				}
			}
		}
		break;

	case KEY_CLEAR:
	case 'V' - CTRL_OFFSET:
		if (!mode_is_sending_active()) {
			ui_clear_main_window();
		}
		break;

	case '[' - CTRL_OFFSET:
	case 'Z' - CTRL_OFFSET:
		state_change_to_idle();
		break;

	case KEY_F (12):
	case PSEUDO_KEYF12:
	case 'C' - CTRL_OFFSET:
		queue_discard_contents();
		cw_flush_tone_queue();
		is_running = false;
		break;

	case KEY_RESIZE:
		state_change_to_idle();
		ui_destroy();
		ui_initialize();
		break;
	}

	/* The command was a recognized interface key. */
	return true;
}





void speed_update(void)
{
	char buffer[CWCP_PARAM_WIDTH];
	snprintf(buffer, CWCP_PARAM_WIDTH, _("%2d WPM"), cw_get_send_speed());
	mvwaddstr(speed_subwindow, 0, 4, buffer);
	wrefresh(speed_subwindow);
	return;
}





void frequency_update(void)
{
	char buffer[CWCP_PARAM_WIDTH];
	snprintf(buffer, CWCP_PARAM_WIDTH, _("%4d Hz"), cw_get_frequency());
	mvwaddstr(tone_subwindow, 0, 3, buffer);
	wrefresh(tone_subwindow);
	return;
}





void volume_update(void)
{
	char buffer[CWCP_PARAM_WIDTH];
	snprintf(buffer, CWCP_PARAM_WIDTH, _("%3d %%"), cw_get_volume());
	mvwaddstr(volume_subwindow, 0, 4, buffer);
	wrefresh(volume_subwindow);
	return;
}





void gap_update(void)
{
	char buffer[CWCP_PARAM_WIDTH];
	int value = cw_get_gap();
	snprintf(buffer, CWCP_PARAM_WIDTH, value == 1 ? _("%2d dot ") : _("%2d dots"), value);
	mvwaddstr(gap_subwindow, 0, 3, buffer);
	wrefresh(gap_subwindow);
	return;
}





/**
   \brief Handle UI event

   Handle an interface 'event', in this case simply a character from
   the keyboard via curses.
*/
void ui_handle_event(int c)
{
	/* See if this character is a valid user interface command. */
	if (interface_interpret(c)) {
		return;
	}

	/* If the character is standard 8-bit ASCII or backspace, and
	   the current sending mode is from the keyboard, then make an
	   effort to either queue the character for sending, or delete
	   the most recently queued. */
	if (mode_is_sending_active() && mode_current_is_type(M_KEYBOARD)) {
		if (c == KEY_BACKSPACE || c == KEY_DC) {
			queue_delete_character();
			return;
		} else if (c <= UCHAR_MAX)  {
			queue_enqueue_character((char) c);
			return;
		}
	}

	/* The 'event' is nothing at all of interest; drop it. */

	return;
}





/**
   \brief Check for keyboard input from user

   Calls our sender polling function at regular intervals, and returns only
   when data is available to getch(), so that it will not block.

   Opportunistically on every poll check if we need to update queue
   of elements to play/display, and if so, do update the queue.

   \param fd - file to pool for new keys from the user
   \param usecs - pooling interval
*/
void ui_poll_user_input(int fd, int usecs)
{
	int fd_count;

	/* Poll until the select indicates data on the file descriptor. */
	do {
		fd_set read_set;
		struct timeval timeout;

		/* Set up a the file descriptor set and timeout information. */
		FD_ZERO(&read_set);
		FD_SET(fd, &read_set);
		timeout.tv_sec = usecs / 1000000;
		timeout.tv_usec = usecs % 1000000;

		/* Wait until timeout, data, or a signal.
		   If a signal interrupts select, we can just treat it as
		   another timeout. */
		fd_count = select(fd + 1, &read_set, NULL, NULL, &timeout);
		if (fd_count == -1 && errno != EINTR) {
			perror("select");
			exit(EXIT_FAILURE);
		}

		/* Make this call on timeouts and on reads; it's just easier. */
		queue_transfer_character_to_libcw();
	} while (fd_count != 1);

	return;
}





void ui_clear_main_window(void)
{
	werase(text_subwindow);
	wmove(text_subwindow, 0, 0);
	wrefresh(text_subwindow);

	return;
}





void ui_refresh_main_window(void)
{
	touchwin(text_subwindow);
	wnoutrefresh(text_subwindow);
	doupdate();

	return;
}





void ui_display_state(const char *state)
{
	box(text_window, 0, 0);
	mvwaddstr(text_window, 0, 1, state);
	wnoutrefresh(text_window);
	doupdate();

	return;
}





/**
   Change appearance of list of modes, indicating current mode

   Change an entry in list of modes, indicating which mode is
   currently selected. un-highlight \p old_mode, highlight
   \p current_mode.

   \param old_mode - index of old mode
   \param current_mode - index of currently selected mode
*/
void ui_update_mode_selection(int old_mode, int current_mode)
{
      wattroff(mode_subwindow, A_REVERSE);
      mvwaddstr(mode_subwindow,
		old_mode, 1,
		mode_get_description(old_mode));

      wattron(mode_subwindow, A_REVERSE);
      mvwaddstr(mode_subwindow,
		current_mode, 1,
		mode_get_description(current_mode));

      wrefresh(mode_subwindow);

      return;
}





/**
   \brief Signal handler for signals, to clear up on kill
*/
void signal_handler(int signal_number)
{
	/* Attempt to wrestle the screen back from curses. */
	ui_destroy();

	/* Show the signal caught, and exit. */
	fprintf(stderr, _("\nCaught signal %d, exiting...\n"), signal_number);
	exit(EXIT_SUCCESS);
}





/**
   Parse the command line, initialize a few things, then enter the
   main program event loop, from which there is no return.
*/
int main(int argc, char **argv)
{
	atexit(cwcp_atexit);

	/* Set locale and message catalogs. */
	i18n_initialize();

	/* Parse combined environment and command line arguments. */
	int combined_argc;
	char **combined_argv;

	/* Parse combined environment and command line arguments. */
	combine_arguments(_("CWCP_OPTIONS"), argc, argv, &combined_argc, &combined_argv);

	config = cw_config_new(cw_program_basename(argv[0]));
	if (!config) {
		return EXIT_FAILURE;
	}
	config->has_practice_time = true;
	config->has_outfile = true;

	if (!cw_process_argv(combined_argc, combined_argv, all_options, config)) {
		fprintf(stderr, _("%s: failed to parse command line args\n"), config->program_name);
		return EXIT_FAILURE;
	}
	free(combined_argv);
	combined_argv = NULL;

	if (!cw_config_is_valid(config)) {
		fprintf(stderr, _("%s: inconsistent arguments\n"), config->program_name);
		return EXIT_FAILURE;
	}

	if (config->input_file) {
		if (!cw_dictionaries_read(config->input_file)) {
			fprintf(stderr, _("%s: %s\n"), config->program_name, strerror(errno));
			fprintf(stderr, _("%s: can't load dictionary from input file %s\n"), config->program_name, config->input_file);
			return EXIT_FAILURE;
		}
	}

	if (config->output_file) {
		if (!cw_dictionaries_write(config->output_file)) {
			fprintf(stderr, _("%s: %s\n"), config->program_name, strerror(errno));
			fprintf(stderr, _("%s: can't save dictionary to output file  %s\n"), config->program_name, config->input_file);
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
		fprintf(stderr, "%s: failed to create generator\n", config->program_name);
		return EXIT_FAILURE;
	}
	timer_set_total_practice_time(config->practice_time);


	static const int SIGNALS[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM, 0 };
	/* Set up signal handlers to clear up and exit on a range of signals. */
	for (int i = 0; SIGNALS[i]; i++) {
		if (!cw_register_signal_handler(SIGNALS[i], signal_handler)) {
			fprintf(stderr, _("%s: can't register signal: %s\n"), config->program_name, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	/* Build our table of modes from dictionaries, augmented with
	   keyboard and any other local modes. */
	mode_initialize();

	/* Initialize the curses user interface, then catch and action
	   every keypress we see.  Before calling getch, wait until
	   data is available on stdin, polling the libcw sender.  At
	   60WPM, a dot is 20ms, so polling for the maximum library
	   speed needs a 10ms (10,000usec) timeout. */
	ui_initialize();
	cw_generator_start();
	while (is_running) {
		ui_poll_user_input(fileno(stdin), 10000);
		ui_handle_event(getch());
	}

	cw_wait_for_tone_queue();

	return EXIT_SUCCESS;
}





void cwcp_atexit(void)
{
	ui_destroy();

	if (generator) {
		cw_complete_reset();
		cw_generator_stop();
		cw_generator_delete();
	}

	mode_clean();

	cw_dictionaries_unload();

	if (config) {
		cw_config_delete(&config);
	}

	return;
}
