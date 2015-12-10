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

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "libcw.h"

#include "dictionary.h"
#include "cw_words.h"
#include "cw_common.h"
#include "memory.h"
#include "i18n.h"


#include "libcw_debug.h"


/**
   cw dictionaries

   Dictionary is a data structure containing list of "words", grouped
   together for some specific purpose.

   Every dictionary has a description (dictionary's label), and a list
   of words that are the body of a dictionary. "words" is used loosely:
   the words can be real, multi-character words, or just single letters.

   Every dictionary has a 'group size' property, telling if words in
   a dictionary are only single-character words, or multi-character words.

   Dictionaries can be stored in a text file between two sessions of
   a program.

   Dictionaries stored in a memory are put on a linked list. There can
   be only one such list at a time.

   Description of a given dictionary is in first line in square brackets,
   and in following line is a list of words. Words are separated by space
   characters. Examples (yes, this looks like contents of .ini file):

   [ Digits ]
   1 2 3 4 5 6 7 8 9 0

   [ A-K ]
   A B C D E F G H I J K

   [ My favourite nouns ]
   coffee C vacation Linux

   Comments in file are allowed (and skipped): lines starting with ';'
   or '#' characters are considered to be comments.

   Usually an application uses several dictionaries, and a file can also
   store few dictionaries.

   The module has its own, internal, default list of cw dictionaries.
   If application doesn't read any dictionaries from text file, the
   module loads its own dictionaries to memory on first call to
   cw_dictionaries_iterate().

   The module implements functions providing following functionality:
   \li loading dictionaries from text file to memory,
   \li removing dictionaries from memory,
   \li writing current dictionaries from memory to text file,
   \li iterating over list of dictionaries in memory,
   \li getting description of a specified dictionary,
   \li getting 'group size' information about given dictionary,
   \li getting a random word from given dictionary.
*/




static void cw_dictionary_trim(char *buffer);
static bool cw_dictionary_parse_is_comment(const char *line);
static bool cw_dictionary_parse_is_section(const char *line, char **name_ptr);

static cw_dictionary_t *cw_dictionaries_create_from_stream(FILE *stream, const char *file);
static cw_dictionary_t *cw_dictionaries_create_default(void);

static char *cw_dictionary_check_line(const char *line);



/*---------------------------------------------------------------------*/
/*  Dictionary data                                                    */
/*---------------------------------------------------------------------*/

enum { MAX_LINE = 8192 };

/* Aggregate dictionary data into a structure. */
struct cw_dictionary_s {
	const char *description;      /* Dictionary description */
	const char *const *wordlist;  /* Dictionary word list */
	int wordlist_length;          /* Length of word list */
	int group_size;               /* Size of a group */

	void *mutable_description;    /* Freeable (aliased) description string */
	void *mutable_wordlist;       /* Freeable (aliased) word list */
	void *mutable_wordlist_data;  /* Freeable bulk word list data */

	dictionary *next;             /* List pointer */
};

/* Head of a list storing currently loaded dictionaries. */
static cw_dictionary_t *dictionaries_head = NULL;


/*---------------------------------------------------------------------*/
/*  Dictionary implementation                                          */
/*---------------------------------------------------------------------*/

/*
 * dictionary_new()
 * dictionary_new_const()
 * dictionary_new_mutable()
 *
 * Create a new dictionary, and add to any list tail passed in, returning
 * the entry created (the new list tail).  The main function adds the data,
 * plus any mutable pointers that need to be freed on destroying the
 * dictionary.  The const and mutable variants are convenience interfaces.
 */
static dictionary *dictionary_new(dictionary *tail,
				  const char *description,
				  const char *const wordlist[],
				  void *mutable_description,
				  void *mutable_wordlist,
				  void *mutable_wordlist_data)
{
	/* Count words in the wordlist, and look for multicharacter
	   entries. */
	int words = 0;
	bool is_multicharacter = false;
	for (int word = 0; wordlist[word]; word++) {
		is_multicharacter |= strlen(wordlist[word]) > 1;
		words++;
	}

	/* Create a new dictionary and fill in the main fields.  Group
	   size is set to one for multicharacter word lists, five
	   otherwise. */
	dictionary *dict = safe_malloc(sizeof (*dict));
	dict->description = description;
	dict->wordlist = wordlist;
	dict->wordlist_length = words;
	dict->group_size = is_multicharacter ? 1 : 5;
	dict->next = NULL;

	/* Add mutable pointers passed in. */
	dict->mutable_description = mutable_description;
	dict->mutable_wordlist = mutable_wordlist;
	dict->mutable_wordlist_data = mutable_wordlist_data;

	/* Add to the list tail passed in, if any. */
	if (tail) {
		tail->next = dict;
	}

	return dict;
}





static dictionary *dictionary_new_const(dictionary *tail,
					const char *description,
					const char *const wordlist[])
{
	return dictionary_new(tail, description, wordlist, NULL, NULL, NULL);
}





static dictionary *dictionary_new_mutable(dictionary *tail,
					  char *description,
					  const char *wordlist[],
					  void *wordlist_data)
{
	return dictionary_new(tail, description, wordlist,
			      description, wordlist, wordlist_data);
}





/**
   \brief Reset dictionaries state

   Free any allocations from the current dictionaries list, and return
   list of dictionaries to the initial state (i.e. empty).
*/
void cw_dictionaries_unload(void)
{
	cw_dictionary_t *next = NULL;

	/* Free each dictionary in the list. */
	for (cw_dictionary_t *entry = dictionaries_head; entry; entry = next) {

		next = entry->next;

		/* Free allocations held in the dictionary. */
		free(entry->mutable_wordlist);
		free(entry->mutable_description);
		free(entry->mutable_wordlist_data);

		/* Free the dictionary itself. */
		free(entry);
	}

	dictionaries_head = NULL;

	return;
}





/*
 * dictionary_unload()
 *
 * Free any allocations from the current dictionary, and return to the
 * initial state.
 */
void dictionary_unload(void)
{
	cw_dictionaries_unload();
	return;
}





/**
   \brief Parse a line, check if it is a line with comment

   Function parses given \p line and checks if it looks like a comment.

   A comment is a line starting with ';' or '#' char, or a line consisting
   entirely of spaces and TAB characters

   \param line - line to parse

   \return true if line is a line with comment
   \return false otherwise
*/
bool cw_dictionary_parse_is_comment(const char *line)
{
	size_t index = strspn(line, " \t");
	return index == strlen(line) || strchr(";#", line[0]);
}





/**
   \brief Parse a line, check if it is section name

   Function parses given \p line and checks if it looks like a name of
   a section (a string in square brackets). If it is a section name,
   then the function stores the name in \p name_ptr and returns true.
   Otherwise it returns false.

   On success the function allocates memory and sets *name_ptr pointer
   to this memory.

   \param line - line to parse
   \param name_ptr - output, place for name of a section

   \return true if line contains section name
   \return false otherwise
*/
bool cw_dictionary_parse_is_section(const char *line, char **name_ptr)
{
	char *name = safe_malloc(strlen(line) + 1);

	char dummy;
	int count = sscanf(line, " [ %[^]] ] %c", name, &dummy);
	if (count == 1) {
		*name_ptr = safe_realloc(name, strlen(name) + 1);
		return true;
	}

	free(name);
	return false;
}





/*
 * dictionary_build_wordlist()
 *
 * Build and return a wordlist from a string of space-separated words.  The
 * wordlist_data is changed by this function.
 */
static const char **
dictionary_build_wordlist (char *wordlist_data)
{
  const char **wordlist, *word;
  int size, allocation;

  /* Split contents into a wordlist, and store each word retrieved. */
  size = allocation = 0;
  wordlist = NULL;
  for (word = strtok (wordlist_data, " \t"); word; word = strtok (NULL, " \t"))
    {
      if (size == allocation)
        {
          allocation = allocation == 0 ? 1 : allocation << 1;
          wordlist = safe_realloc (wordlist, sizeof (*wordlist) * allocation);
        }

      wordlist[size++] = word;
    }

  /* Add a null sentinel. */
  if (size == allocation)
    {
      allocation++;
      wordlist = safe_realloc (wordlist, sizeof (*wordlist) * allocation);
    }
  wordlist[size++] = NULL;

  return wordlist;
}





/**
   \brief Trim a line

   Helper functions for cw_dictionaries_read().
   Trims a line of all leading and trailing whitespace.
   Trimming is done in-place, content of the \p buffer argument may
   be modified.

   \param buffer - buffer with string to trim
*/
void cw_dictionary_trim(char *buffer)
{
	size_t bytes = strlen(buffer);
	while (bytes > 0 && isspace(buffer[bytes - 1])) {
		buffer[--bytes] = '\0';
	}

	size_t index = strspn(buffer, " \t");
	if (index > 0) {
		memmove(buffer, buffer + index, bytes - index + 1);
	}

	return;
}





/**
   \brief Check if given line contains any invalid characters.

   Check a \p line for unsendable characters.

   If there are any invalid characters in \p line, return an allocated string with
   '^' characters in error positions and with spaces in all other positions.
   Return NULL if all characters in \p line are valid (sendable).

   Returned pointer is managed by caller.

   \param line - line to check

   \return pointer to string if \p line contains one or more invalid characters
   \return NULL otherwise
*/
char *cw_dictionary_check_line(const char *line)
{
	char *errors = malloc(strlen(line) + 1);

	int count = 0;
	int i = 0;
	for (i = 0; line[i] != '\0'; i++) {
		errors[i] = cw_character_is_valid(line[i]) ? ' ' : '^';
		if (errors[i] == '^') {
			count++;
		}
	}
	errors[i] = '\0';

	/* If not all sendable, return the string, otherwise return NULL. */
	if (count > 0) {
		return errors;
	}

	free(errors);
	errors = NULL;

	return NULL;
}





/**
   \brief Create a dictionary list from a stream

   Load dictionaries from a \p stream into memory. The \p stream needs to
   be already open for reading. \p file is a name of the stream, used in
   error messages. \p stream should be open and closed by caller of the
   function.

   The file format is expected to be ini-style.

   This is a lower level function, to be used by cw_dictionaries_read().

   \param stream - stream to read data from
   \param file - human-readable name of the stream

   \return head of list of loaded dictionaries on success
   \return NULL if loading fails.
*/
cw_dictionary_t *cw_dictionaries_create_from_stream(FILE *stream, const char *file)
{
	const char **wordlist;

	/* Clear the variables used to accumulate stream data. */
	char *line = safe_malloc(MAX_LINE);
	int line_number = 0;
	char *name = NULL;
	char *content = NULL;
	dictionary *head = NULL;
	dictionary *tail = NULL;

	/* Parse input lines to create a new dictionary. */
	while (cw_getline(stream, line, MAX_LINE + 1)) {
		line_number++;

		char *new_name;

		if (cw_dictionary_parse_is_comment(line)) {
			continue;
		} else if (cw_dictionary_parse_is_section(line, &new_name)) {
			/* New section, so handle data accumulated so far.
			   Or if no data accumulated, forget it. */
			if (content) {
				wordlist = dictionary_build_wordlist(content);
				tail = dictionary_new_mutable(tail, name, wordlist, content);
				head = head ? head : tail;
			} else {
				free(name);
				name = NULL;
			}

			/* Start new accumulation of words. */
			cw_dictionary_trim(new_name);
			name = new_name;
			content = NULL;
		} else if (name) {
			/* Check the line for unsendable characters. */
			char *errors = cw_dictionary_check_line(line);
			if (errors) {
				fprintf(stderr, "%s:%d: unsendable character found:\n",
					file, line_number);
				fprintf(stderr, "%s\n%s\n", line, errors);
				free(errors);
			}

			/* Accumulate this line into the current content. */
			cw_dictionary_trim(line);
			if (content) {
				content = safe_realloc(content,
						       strlen(content) + strlen(line) + 2);
				strcat(content, " ");
				strcat(content, line);
			} else {
				content = safe_malloc(strlen(line) + 1);
				strcpy(content, line);
			}
		} else {
			fprintf(stderr,
				"%s:%d: unrecognized line, expected [section] or commentary\n",
				file, line_number);
		}
	}

	/* Handle any final accumulated data. */
	if (content) {
		wordlist = dictionary_build_wordlist(content);
		tail = dictionary_new_mutable(tail, name, wordlist, content);
		head = head ? head : tail;
	}

	if (!head) {
		fprintf(stderr,
			"%s:%d: no usable dictionary data found in the file\n",
			file, line_number);
	}

	free(line);
	line = NULL;

	return head;
}





/**
   \brief Create list of default dictionaries

   Create a dictionary list from internal data.

   \return head of the list on success
   \return NULL on failure
*/
cw_dictionary_t *cw_dictionaries_create_default(void)
{
	cw_dictionary_t *head = NULL, *tail = NULL;

	head = dictionary_new_const (NULL, _("Letter Groups"), CW_ALPHABETIC);
	tail = dictionary_new_const (head, _("Number Groups"), CW_NUMERIC);
	tail = dictionary_new_const (tail, _("Alphanum Groups"), CW_ALPHANUMERIC);
	tail = dictionary_new_const (tail, _("All Char Groups"), CW_ALL_CHARACTERS);
	tail = dictionary_new_const (tail, _("English Words"), CW_SHORT_WORDS);
	tail = dictionary_new_const (tail, _("CW Words"), CW_CW_WORDS);
	tail = dictionary_new_const (tail, _("PARIS Calibrate"), CW_PARIS);
	tail = dictionary_new_const (tail, _("EISH5 Groups"), CW_EISH5);
	tail = dictionary_new_const (tail, _("TMO0 Groups"), CW_TMO0);
	tail = dictionary_new_const (tail, _("AUV4 Groups"), CW_AUV4);
	tail = dictionary_new_const (tail, _("NDB6 Groups"), CW_NDB6);
	tail = dictionary_new_const (tail, _("KX=-RP Groups"), CW_KXffRP);
	tail = dictionary_new_const (tail, _("FLYQC Groups"), CW_FLYQC);
	tail = dictionary_new_const (tail, _("WJ1GZ Groups"), CW_WJ1GZ);
	tail = dictionary_new_const (tail, _("23789 Groups"), CW_23789);
	tail = dictionary_new_const (tail, _(",?.;)/ Groups"), CW_FIGURES_1);
	tail = dictionary_new_const (tail, _("\"'$(+:_ Groups"), CW_FIGURES_2);

#if 0
	/* test code */

	dictionaries_head = head;
	int i = 0;
	for (cw_dictionary_t *dict = cw_dictionaries_iterate(NULL);
	     dict;
	     dict = cw_dictionaries_iterate(dict)) {



		i++;
	}

	fprintf(stderr, "number of default dictionaries = %d (should be 17)\n", i + 1);

	if (i == 17) {
		return head;
	} else {
		cw_dictionaries_unload();
		return NULL;
	}
#else
	return head;
#endif
}





/**
   \brief Read dictionaries from given file

   Set the main dictionary list to data read from a file.

   \param file - open file to read from

   \return true on success
   \return false if loading fails
*/
bool cw_dictionaries_read(const char *file)
{
	/* Open the input stream, or fail if unopenable. */
	FILE *stream = fopen(file, "r");
	if (!stream) {
		fprintf(stderr, "%s: open error: %s\n", file, strerror(errno));
		return false;
	}

	/* If we can generate a dictionary list, free any currently
	   allocated one and store the details of what we loaded into
	   module variables. */
	cw_dictionary_t *head = cw_dictionaries_create_from_stream(stream, file);
	if (head) {
		cw_dictionaries_unload();
		dictionaries_head = head;
	}

	/* Close stream and return true if we loaded a dictionary. */
	fclose(stream);
	return head != NULL;
}





int dictionary_load(const char *file)
{
	return cw_dictionaries_read(file);
}





/**
   \brief Iterate known dictionaries

   Because this is the only way dictionaries can be accessed by callers,
   this function sets up a default dictionary list if none loaded.

   \param current - current dictionary on list of dictionaries, or NULL if you want to fetch first dictionary from list

   \return the first dictionary if \p current is NULL
   \return next dictionary is \p current is a dictionary
   \return NULL if there are no more dictionaries on list
*/
const cw_dictionary_t *cw_dictionaries_iterate(const cw_dictionary_t *current)
{
	/* If no dictionary list has been loaded, supply a default one. */
	if (!dictionaries_head) {
		dictionaries_head = cw_dictionaries_create_default();
	}

	return current ? current->next : dictionaries_head;
}





/*
 * dictionary_iterate()
 *
 * Iterate known dictionaries.  Returns the first if dictionary is NULL,
 * otherwise the next, or NULL if no more.
 *
 * Because this is the only way dictionaries can be accessed by callers,
 * this function sets up a default dictionary list if none loaded.
 */
const dictionary *dictionary_iterate(const dictionary *current)
{
	return cw_dictionaries_iterate(current);
}





/**
   \brief Write current dictionaries to given file

   Write the currently loaded (or default) dictionaries out to a given file.

   \param file - file to write to

   \return true on success
   \return false if writing fails
*/
bool cw_dictionaries_write(const char *file)
{
	/* Open the output stream, or fail if unopenable. */
	FILE *stream = fopen(file, "w");
	if (!stream) {
		fprintf(stderr, "%s: open error: %s\n", file, strerror(errno));
		return false;
	}

	/* If no dictionary list has been loaded, supply a default one,
	   then print details of each. */
	if (!dictionaries_head) {
		dictionaries_head = cw_dictionaries_create_default();
	}

	for (const cw_dictionary_t *dict = dictionaries_head; dict; dict = dict->next) {
		fprintf(stream, "[ %s ]\n\n", dict->description);

		int chars = 0;
		for (int i = 0; i < dict->wordlist_length; i++) {
			fprintf(stream, " %s", dict->wordlist[i]);
			chars += strlen(dict->wordlist[i]) + 1;
			if (chars > 72) {
				fprintf(stream, "\n");
				chars = 0;
			}
		}
		fprintf(stream, chars > 0 ? "\n\n" : "\n");
	}

	fclose(stream);
	return true;
}





int dictionary_write(const char *file)
{
	return cw_dictionaries_write(file);
}





/**
   \brief Get description of a given dictionary

   \return a string
*/
const char *cw_dictionary_get_description(const cw_dictionary_t *dict)
{
	return dict->description;
}





/*
 * get_dictionary_description()
 * get_dictionary_group_size()
 *
 * Return the text description and group size for a given dictionary.
 */
const char *get_dictionary_description(const dictionary *dict)
{
	return dict->description;
}





/**
   \brief Get group size of a given dictionary

   \param dict - dictionary to query
*/
int cw_dictionary_get_group_size(const cw_dictionary_t *dict)
{
	return dict->group_size;
}





int get_dictionary_group_size(const dictionary *dict)
{
	return dict->group_size;
}





/**
   \brief Get a random word from given dictionary

   \param dict - dictionary to query

   \return a string
*/
const char *cw_dictionary_get_random_word(const cw_dictionary_t *dict)
{
	static bool is_initialized = false;

	/* On the first call, seed the random number generator. */
	if (!is_initialized) {
		srand(time(NULL));
		is_initialized = true;
	}

	return dict->wordlist[rand() % dict->wordlist_length];
}





/*
 * get_dictionary_random_word()
 *
 * Return a random word from the given dictionary.
 */
const char *get_dictionary_random_word(const dictionary *dict)
{
	return cw_dictionary_get_random_word(dict);
}





#ifdef CW_DICTIONARY_UNIT_TESTS


#include "libcw_test.h"


static unsigned int test_cw_dictionary_check_line(void);


typedef unsigned int (*cw_dict_test_function_t)(void);

static cw_dict_test_function_t cw_dict_unit_tests[] = {
	test_cw_dictionary_check_line,
	NULL
};



int main(void)
{
	fprintf(stderr, "unit tests for \"dictionary\" functions\n\n");

	int i = 0;
	while (cw_dict_unit_tests[i]) {
		cw_dict_unit_tests[i]();
		i++;
	}

	/* "make check" facility requires this message to be
	   printed on stdout; don't localize it */
	fprintf(stdout, "\ndictionary: test result: success\n\n");

	return 0;
}





unsigned int test_cw_dictionary_check_line(void)
{
	int p = fprintf(stderr, "dictionary: cw_dictionary_check_line():");

	/* First test the function giving it strings with some invalid
	   characters. */
	{
		struct {
			const char *in;
			const char *expected_out;
		} invalid_test_data[] = {
			/* See src/libcw/libcw.c for list of valid characters. */

			/* The '^' marks in "out" strings point
			   invalid characters. These "out" strings
			   look exactly as expected outputs of tested
			   function. */

			{ "t7890pl\tnbf7890lnbdg",
			  "       ^            " },

			{ "`u90-pkngfrt6789=0ol",
			  "^                   " },

			{ "34rtghjm,1qazx-poijh|",
			  "                    ^" },

			{ "=-0987654$[^&*()%^&|",
			  "          ^  ^  ^  ^" },

			{ NULL, NULL } /* Guard. */
		};


		for (int i = 0; invalid_test_data[i].in; i++) {
			char *out = cw_dictionary_check_line(invalid_test_data[i].in);

			cw_assert (out, "function returned NULL for string #%d with invalid char(s)", i);

			cw_assert (!strcmp(out, invalid_test_data[i].expected_out),
				   "strings not equal:\n\"%s\"\n\"%s\"\n",
				   out, invalid_test_data[i].expected_out);

			free(out);
			out = NULL;
		}
	}



	/* Now also test with strings with all chars valid. */
	{
		const char *valid_test_data[] = {
			/* See src/libcw/libcw.c for list of valid characters. */

			"t7890pltnbf7890lnbdg",
			"u90-pkngfrt6789=0ol",
			"34rtghjm,1qazx-poijh",
			"=-0987654$^&()^&ABCP",

			NULL /* Guard. */
		};


		for (int i = 0; valid_test_data[i]; i++) {
			char *out = cw_dictionary_check_line(valid_test_data[i]);

			cw_assert (!out, "function returned non-NULL for string #%d with all valid chars", i);
		}
	}


	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}



#endif /* #ifdef CW_DICTIONARY_UNIT_TESTS */
