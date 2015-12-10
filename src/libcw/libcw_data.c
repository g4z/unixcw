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
   \file libcw_data.c

   \brief Characters, representations, lookup and validation functions.

   The only hard data stored by libcw library seems to be:
   \li characters and their representations
   \li procedural signals
   \li phonetics

   These three groups of data, collected in three separate tables, are
   defined in this file, together with lookup functions and other
   related utility functions.

   Unit test functions for this code are at the end of the file.
*/





#include <stdio.h>
#include <limits.h> /* UCHAR_MAX */
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>


#include "libcw.h"
#include "libcw_debug.h"
#include "libcw_data.h"
#include "libcw_test.h"





extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;






/*
  Morse code characters table.  This table allows lookup of the Morse
  representation (shape) of a given alphanumeric character.
  Representations (shapes) are held as a string, with "-" representing
  dash, and "." representing dot.  The table ends with a NULL entry.

  Notice that ASCII characters are stored as uppercase characters.
*/

static const cw_entry_t CW_TABLE[] = {
	/* ASCII 7bit letters */
	{'A', ".-"  },  {'B', "-..."},  {'C', "-.-."},
	{'D', "-.." },  {'E', "."   },  {'F', "..-."},
	{'G', "--." },  {'H', "...."},  {'I', ".."  },
	{'J', ".---"},  {'K', "-.-" },  {'L', ".-.."},
	{'M', "--"  },  {'N', "-."  },  {'O', "---" },
	{'P', ".--."},  {'Q', "--.-"},  {'R', ".-." },
	{'S', "..." },  {'T', "-"   },  {'U', "..-" },
	{'V', "...-"},  {'W', ".--" },  {'X', "-..-"},
	{'Y', "-.--"},  {'Z', "--.."},

	/* Numerals */
	{'0', "-----"},  {'1', ".----"},  {'2', "..---"},
	{'3', "...--"},  {'4', "....-"},  {'5', "....."},
	{'6', "-...."},  {'7', "--..."},  {'8', "---.."},
	{'9', "----."},

	/* Punctuation */
	{'"', ".-..-."},  {'\'', ".----."},  {'$', "...-..-"},
	{'(', "-.--." },  {')',  "-.--.-"},  {'+', ".-.-."  },
	{',', "--..--"},  {'-',  "-....-"},  {'.', ".-.-.-" },
	{'/', "-..-." },  {':',  "---..."},  {';', "-.-.-." },
	{'=', "-...-" },  {'?',  "..--.."},  {'_', "..--.-" },
	{'@', ".--.-."},

	/* ISO 8859-1 accented characters */
	{'\334', "..--" },   /* U with diaeresis */
	{'\304', ".-.-" },   /* A with diaeresis */
	{'\307', "-.-.."},   /* C with cedilla */
	{'\326', "---." },   /* O with diaeresis */
	{'\311', "..-.."},   /* E with acute */
	{'\310', ".-..-"},   /* E with grave */
	{'\300', ".--.-"},   /* A with grave */
	{'\321', "--.--"},   /* N with tilde */

	/* ISO 8859-2 accented characters */
	{'\252', "----" },   /* S with cedilla */
	{'\256', "--..-"},   /* Z with dot above */

	/* Non-standard procedural signal extensions to standard CW characters. */
	{'<', "...-.-" },    /* VA/SK, end of work */
	{'>', "-...-.-"},    /* BK, break */
	{'!', "...-."  },    /* SN, understood */
	{'&', ".-..."  },    /* AS, wait */
	{'^', "-.-.-"  },    /* KA, starting signal */
	{'~', ".-.-.." },    /* AL, paragraph */

	/* Guard. */
	{0, NULL}
};





/**
   \brief Return the number of characters present in character lookup table

   Return the number of characters that are known to libcw.
   The number includes:
   \li ASCII 7bit letters,
   \li numerals,
   \li punctuation,
   \li ISO 8859-1 accented characters,
   \li ISO 8859-2 accented characters,
   \li non-standard procedural signal extensions to standard CW characters.

   testedin::test_character_lookups_internal()

   \return number of characters known to libcw
*/
int cw_get_character_count(void)
{
	static int character_count = 0;

	if (character_count == 0) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			character_count++;
		}
	}

	return character_count;
}





/**
   \brief Get list of characters present in character lookup table

   Function provides a string containing all of the characters represented
   in library's lookup table.

   The list includes:
   \li ASCII 7bit letters,
   \li numerals,
   \li punctuation,
   \li ISO 8859-1 accented characters,
   \li ISO 8859-2 accented characters,
   \li non-standard procedural signal extensions to standard CW characters.

   \p list should be allocated and managed by caller.
   The length of \p list must be at least one greater than the number
   of characters represented in the character lookup table, returned
   by cw_get_character_count().

   testedin::test_character_lookups_internal()

   \param list - pointer to space to be filled by function
*/
void cw_list_characters(char *list)
{
	cw_assert (list, "Output pointer is null");

	/* Append each table character to the output string. */
	int i = 0;
	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
		list[i++] = cw_entry->character;
	}

	list[i] = '\0';

	return;
}





/**
   \brief Get length of the longest representation

   Function returns the string length of the longest representation in the
   character lookup table.

   testedin::test_character_lookups_internal()

   \return a positive number - length of the longest representation
*/
int cw_get_maximum_representation_length(void)
{
	static int maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest representation. */
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			int length = (int) strlen (cw_entry->representation);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return maximum_length;
}





/**
   \brief Return representation of given character

   Look up the given character \p c, and return the representation of
   that character.  Return NULL if there is no representation for the given
   character. Otherwise return pointer to static string with representation
   of character.

   The returned pointer is owned and managed by library.

   \param c - character to look up

   \return pointer to string with representation of character on success
   \return NULL on failure (when \p c has no representation)
*/
const char *cw_character_to_representation_internal(int c)
{
	static const cw_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to give
	   direct access to the CW table for a given character. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize fast lookup table");

		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			lookup[(unsigned char) cw_entry->character] = cw_entry;
		}

		is_initialized = true;
	}

	/* There is no differentiation in the lookup and
	   representation table between upper and lower case
	   characters; everything is held as uppercase.  So before we
	   do the lookup, we convert to ensure that both cases
	   work. */
	c = toupper(c);

	/* Now use the table to lookup the table entry.  Unknown characters
	   return NULL, courtesy of the fact that explicitly uninitialized
	   static variables are initialized to zero, so lookup[x] is NULL
	   if it's not assigned to in the above loop. */
	const cw_entry_t *cw_entry = lookup[(unsigned char) c];

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_entry) {
			fprintf(stderr, "libcw: lookup '%c' returned <'%c':\"%s\">\n",
				c, cw_entry->character, cw_entry->representation);
		} else if (isprint(c)) {
			fprintf(stderr, "libcw: lookup '%c' found nothing\n", c);
		} else {
			fprintf(stderr, "libcw: lookup 0x%02x found nothing\n",
				(unsigned char) c);
		}
	}

	return cw_entry ? cw_entry->representation : NULL;
}





/**
   \brief Get representation of a given character

   The function is depreciated, use cw_character_to_representation() instead.

   Return the string representation (shape) of a given Morse code
   character \p c.

   The routine returns CW_SUCCESS on success, and fills in the string
   pointer (\p representation) passed in.
   On failure, it returns CW_FAILURE and sets errno to ENOENT,
   indicating that the character \p c could not be found.

   The length of \p representation buffer must be at least one greater
   than the length of longest representation held in the character
   lookup table. The largest value of length is returned by
   cw_get_maximum_representation_length().

   \param c - character to look up
   \param representation - pointer to space for representation of character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_character(char c, char *representation)
{
	/* Lookup the character, and if found, return the string. */
	const char *retval = cw_character_to_representation_internal(c);
	if (retval) {
		if (representation) {
			strcpy(representation, retval);
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested character. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Get representation of a given character

   On success return representation of a given character.
   Returned pointer is owned by caller of the function.

   On failure function returns NULL and sets errno:
   ENOENT indicates that the character could not be found.
   ENOMEM indicates that character has been found, but function failed
   to strdup() representation.

   testedin::test_character_lookups_internal()

   \param c - character to look up

   \return pointer to freshly allocated representation on success
   \return NULL on failure
*/
char *cw_character_to_representation(int c)
{
	/* Lookup the character, and if found, return the string */
	const char *representation = cw_character_to_representation_internal(c);
	if (representation) {
		char *r = strdup(representation);
		if (r) {
			return r;
		} else {
			errno = ENOMEM;
			return NULL;
		}
	} else {
		/* Failed to find the requested character */
		errno = ENOENT;
		return NULL;
	}
}





/**
   \brief Return a hash value of a character representation

   Return a hash value, in the range 2-255, for a character's
   \p representation.  The routine returns 0 if no valid hash could
   be made from the \p representation string.

   This hash algorithm is designed ONLY for valid CW representations; that is,
   strings composed of only "." and "-", and in this case, strings no longer
   than seven characters.

   TODO: create unit test that verifies that the longest
   representation recognized by libcw is in fact no longer than 7.

   TODO: consider creating an implementation that has the limit larger
   than 7. Then perhaps make the type of returned value to be uint16_t.

   The algorithm simply turns the representation string into a number,
   a "bitmask", based on pattern of "." and "-" in \p representation.
   The first bit set in the mask indicates the start of data (hence
   the 7-character limit) - it is not the data itself.  This mask is
   viewable as an integer in the range 2 (".") to 255 ("-------"), and
   can be used as an index into a fast lookup array.

   testedin::test_cw_representation_to_hash_internal()

   \param representation - string representing a character

   \return non-zero value of hash of valid representation (in range 2-255)
   \return zero for invalid representation
*/
unsigned int cw_representation_to_hash_internal(const char *representation)
{
	/* Our algorithm can handle only 7 characters of representation.
	   And we insist on there being at least one character, too.  */
	int length = (int) strlen (representation);
	if (length > CHAR_BIT - 1 || length < 1) {
		return 0;
	}

	/* Build up the hash based on the dots and dashes; start at 1,
	   the sentinel * (start) bit. */
	unsigned int hash = 1;
	for (int i = 0; i < length; i++) {
		/* Left-shift everything so far. */
		hash <<= 1;

		if (representation[i] == CW_DASH_REPRESENTATION) {
			/* Dash is represented by '1' in hash. */
			hash |= 1;
		} else if (representation[i] == CW_DOT_REPRESENTATION) {
			/* Dot is represented by '0' in hash (we don't
			   have to do anything at this point, the zero
			   is already in the hash). */
			;
		} else {
			/* Invalid element in representation string. */
			return 0;
		}
	}

	return hash;
}





/**
   \brief Return character corresponding to given representation

   Look up the given \p representation, and return the character that it
   represents.

   testedin::test_cw_representation_to_character_internal()

   \param representation - representation of a character to look up

   FIXME: function should be able to return zero as non-error value (?).

   \return zero if there is no character for given representation
   \return non-zero character corresponding to given representation otherwise
*/
int cw_representation_to_character_internal(const char *representation)
{
	static const cw_entry_t *lookup[UCHAR_MAX];   /* Fast lookup table */
	static bool is_complete = true;               /* Set to false if there are any
							 lookup table entries not in
							 the fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to give direct
	   access to the CW table for a hashed representation. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize hash lookup table");
		is_complete = cw_representation_lookup_init_internal(lookup);
		is_initialized = true;
	}

	/* Hash the representation to get an index for the fast lookup. */
	unsigned int hash = cw_representation_to_hash_internal(representation);

	const cw_entry_t *cw_entry = NULL;
	/* If the hashed lookup table is complete, we can simply believe any
	   hash value that came back.  That is, we just use what is at the index
	   "hash", since this is either the entry we want, or NULL. */
	if (is_complete) {
		cw_entry = lookup[hash];
	} else {
		/* Impossible, since test_cw_representation_to_hash_internal()
		   passes without problems for all valid representations.

		   Debug message is already displayed in
		   cw_representation_lookup_init_internal(). */

		/* The lookup table is incomplete, but it doesn't have
		   to be that we are missing entry for this particular
		   hash.

		   Try to find the entry in lookup table anyway, maybe
		   it exists. */
		if (hash && lookup[hash] && lookup[hash]->representation
		    && strcmp(lookup[hash]->representation, representation) == 0) {
			/* Found it in an incomplete table. */
			cw_entry = lookup[hash];
		} else {
			/* We have no choice but to search the table entry
			   by entry, sequentially, from top to bottom. */
			for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
				if (strcmp(cw_entry->representation, representation) == 0) {
					break;
				}
			}

			/* If we got to the end of the table, prepare to return zero. */
			cw_entry = cw_entry->character ? cw_entry : NULL;
		}
	}

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_entry) {
			fprintf(stderr, "libcw: lookup [0x%02x]'%s' returned <'%c':\"%s\">\n",
				hash, representation,
				cw_entry->character, cw_entry->representation);
		} else {
			fprintf(stderr, "libcw: lookup [0x%02x]'%s' found nothing\n",
				hash, representation);
		}
	}

	return cw_entry ? cw_entry->character : 0;
}





/**
   \brief Return character corresponding to given representation

   Look up the given \p representation, and return the character that it
   represents.

   In contrast to cw_representation_to_character_internal(), this
   function doesn't use fast lookup table. It directly traverses the
   main character/representation table and searches for a character.

   The function shouldn't be used in production code.

   Its first purpose is to verify correctness of
   cw_representation_to_character_internal() (since this direct method
   is simpler and, well, direct) in a unit test function.

   The second purpose is to compare time of execution of the two
   functions: direct and with lookup table, and see what are the speed
   advantages of using function with lookup table.

   \param representation - representation of a character to look up

   FIXME: function should be able to return zero as non-error value (?).

   \return zero if there is no character for given representation
   \return non-zero character corresponding to given representation otherwise
*/
int cw_representation_to_character_direct_internal(const char *representation)
{
	/* Search the table entry by entry, sequentially, from top to
	   bottom. */
	const cw_entry_t *cw_entry = NULL;
	for (cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
		if (strcmp(cw_entry->representation, representation) == 0) {
			break;
		}
	}
	/* If we got to the end of the table, prepare to return zero. */
	cw_entry = cw_entry->character ? cw_entry : NULL;

	return cw_entry ? cw_entry->character : 0;
}





/**
   \brief Initialize representation lookup table

   Initialize \p lookup table with values from CW_TABLE (of type cw_entry_t).
   The table is indexed with hashed representations of cw_entry_t->representation
   strings.

   \p lookup table must be large enough to store all entries, caller must
   make sure that the condition is met.

   On failure function returns CW_FAILURE.
   On success the function returns CW_SUCCESS. Successful execution of
   the function is when all representations from CW_TABLE have valid
   hashes, and all entries from CW_TABLE have been put into \p lookup.

   First condition of function's success, mentioned above, should be
   always true because the CW_TABLE has been created once and it
   doesn't change, and all representations in the table are valid.
   Second condition of function's success, mentioned above, should be
   also always true because first condition is always true.

   The initialization may fail under one condition: if the lookup
   functions should operate on non-standard character table, other
   than CW_TABLE. For now it's impossible, because there is no way for
   client code to provide its own table of CW characters.

   \param lookup - lookup table to be initialized

   \return CW_SUCCESS on success
   \return CW_FAILURE otherwise
*/
int cw_representation_lookup_init_internal(const cw_entry_t *lookup[])
{
	/* For each main table entry, create a hash entry.  If the hashing
	   of any entry fails, note that the table is not complete and ignore
	   that entry for now (for the current lookup table, this should not
	   happen).  The hashed table speeds up lookups of representations by
	   a factor of 5-10.

	   NOTICE: Notice that the lookup table will be marked as
	   incomplete only if one or more representations in CW_TABLE
	   aren't valid (i.e. they are made of anything more than '.'
	   or '-'). This wouldn't be a logic error, this would be an
	   error with invalid input. Such invalid input shouldn't
	   happen in properly built characters table.

	   So perhaps returning "false" tells us more about input
	   CW_TABLE than about lookup table.

	   Other possibility to consider is that "is_complete = false"
	   when length of representation is longer than 7
	   dots/dashes. There is an assumption that no representation
	   in input CW_TABLE is longer than 7 dots/dashes. */

	bool is_complete = true;
	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
		unsigned int hash = cw_representation_to_hash_internal(cw_entry->representation);
		if (hash) {
			lookup[hash] = cw_entry;
		} else {
			is_complete = false;
		}
        }

	if (!is_complete) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_WARNING,
			      "libcw: hash lookup table incomplete");
	}

	return is_complete ? CW_SUCCESS : CW_FAILURE;
}





/**
   \brief Check if representation of a character is valid

   This function is depreciated, use cw_representation_is_valid() instead.

   Check that the given string is a valid Morse representation.
   A valid string is one composed of only "." and "-" characters.

   If representation is invalid, function returns CW_FAILURE and sets
   errno to EINVAL.

   \param representation - representation of a character to check

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_check_representation(const char *representation)
{
	bool v = cw_representation_is_valid(representation);
	return v ? CW_SUCCESS : CW_FAILURE;
}





/**
   \brief Check if representation of a character is valid

   Check that the given string is a valid Morse representation.
   A valid string is one composed of only "." and "-" characters.
   This means that the function checks only if representation is
   error-free, and not whether the representation represents
   existing/defined character.

   If representation is invalid, function returns false and sets
   errno to EINVAL.

   testedin::test_validate_representation_internal()

   \param representation - representation of a character to check

   \return true on success
   \return false on failure
*/
bool cw_representation_is_valid(const char *representation)
{
	/* Check the characters in representation. */
	for (int i = 0; representation[i]; i++) {

		if (representation[i] != CW_DOT_REPRESENTATION
		    && representation[i] != CW_DASH_REPRESENTATION) {

			errno = EINVAL;
			return false;
		}
	}

	return true;
}





/**
   \brief Get the character represented by a given Morse representation

   This function is depreciated, use cw_representation_to_character() instead.

   Function checks \p representation, and if it is valid and
   represents a known character, function returns CW_SUCCESS. Additionally,
   if \p c is non-NULL, function puts the looked up character in \p c.

   \p c should be allocated by caller. Function assumes that \p c being NULL
   pointer is a valid situation, and can return CW_SUCCESS in such situation.

   On error, function returns CW_FAILURE. errno is set to EINVAL if any
   character of the representation is invalid, or ENOENT to indicate that
   the character represented by \p representation could not be found.

   \param representation - representation of a character to look up
   \param c - location where to put looked up character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_representation(const char *representation, char *c)
{
	/* Check the characters in representation. */
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return CW_FAILURE;
	}

	/* Lookup the representation, and if found, return the character. */
	char character = cw_representation_to_character_internal(representation);
	if (character) {
		if (c) {
			*c = character;
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested representation. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Return the character represented by a given Morse representation

   Function checks \p representation, and if it is valid and represents
   a known character, function returns the character (a non-zero value).

   On error, function returns zero. errno is set to EINVAL if any
   character of the representation is invalid, or ENOENT to indicate that
   the character represented by \p representation could not be found.

   testedin::test_character_lookups_internal()

   \param representation - representation of a character to look up

   \return non-zero character on success
   \return zero on failure
*/
int cw_representation_to_character(const char *representation)
{
	/* Check the characters in representation. */
	if (!cw_representation_is_valid(representation)) {
		errno = EINVAL;
		return 0;
	}

	/* Lookup the representation, and if found, return the character. */
	int c = cw_representation_to_character_internal(representation);
	if (c) {
		return c;
	} else {
		/* Failed to find the requested representation. */
		errno = ENOENT;
		return 0;
	}
}





/* ******************************************************************** */
/*   Section:Extended Morse code data and lookup (procedural signals)   */
/* ******************************************************************** */





/* Ancillary procedural signals table.  This table maps procedural signal
   characters in the main table to their expansions, along with a flag noting
   if the character is usually expanded for display. */
typedef struct {
	const char character;            /* Character represented */
	const char *const expansion;     /* Procedural expansion of the character */
	const bool is_usually_expanded;  /* If expanded display is usual */
} cw_prosign_entry_t;





static const cw_prosign_entry_t CW_PROSIGN_TABLE[] = {
	/* Standard procedural signals */
	{'"', "AF",  false},   {'\'', "WG", false},  {'$', "SX",  false},
	{'(', "KN",  false},   {')', "KK",  false},  {'+', "AR",  false},
	{',', "MIM", false},   {'-', "DU",  false},  {'.', "AAA", false},
	{'/', "DN",  false},   {':', "OS",  false},  {';', "KR",  false},
	{'=', "BT",  false},   {'?', "IMI", false},  {'_', "IQ",  false},
	{'@', "AC",  false},

	/* Non-standard procedural signal extensions to standard CW characters. */
	{'<', "VA", true},  /* VA/SK, end of work */
	{'>', "BK", true},  /* BK, break */
	{'!', "SN", true},  /* SN, understood */
	{'&', "AS", true},  /* AS, wait */
	{'^', "KA", true},  /* KA, starting signal */
	{'~', "AL", true},  /* AL, paragraph */

	/* Sentinel end of table value */
	{0,   NULL, false}
};





/**
   \brief Get number of procedural signals

   testedin::test_prosign_lookups_internal()

   \return the number of characters represented in the procedural signal expansion lookup table
*/
int cw_get_procedural_character_count(void)
{
	static int character_count = 0;

	if (character_count == 0) {

		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			character_count++;
		}
	}

	return character_count;
}





/**
   \brief Get list of characters for which procedural expansion is available

   Function returns into \p list a string containing all of the Morse
   characters for which procedural expansion is available.  The length
   of \p list must be at least by one greater than the number of
   characters represented in the procedural signal expansion lookup
   table, returned by cw_get_procedural_character_count().

   \p list is managed by caller

   testedin::test_prosign_lookups_internal()

   \param list - space for returned characters
*/
void cw_list_procedural_characters(char *list)
{
	/* Append each table character to the output string. */
	int i = 0;
	for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
		list[i++] = e->character;
	}

	list[i] = '\0';

	return;
}





/**
   \brief Get length of the longest procedural expansion

   Function returns the string length of the longest expansion
   in the procedural signal expansion table.

   testedin::test_prosign_lookups_internal()

   \return length
*/
int cw_get_maximum_procedural_expansion_length(void)
{
	static size_t maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest. */
		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			size_t length = strlen(e->expansion);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return (int) maximum_length;
}





/**
   \brief Return information related to a procedural character

   Function looks up the given procedural character \p c, and returns the
   expansion of that procedural character, with a display hint in
   \p is_usually_expanded.

   Pointer returned by the function is owned and managed by library.
   \p is_usually_expanded pointer is owned by client code.

   \param c - character to look up
   \param is_usually_expanded - output, display hint

   \return expansion of input character on success
   \return NULL if there is no table entry for the given character
*/
const char *cw_lookup_procedural_character_internal(int c, bool *is_usually_expanded)
{
	static const cw_prosign_entry_t *lookup[UCHAR_MAX];  /* Fast lookup table */
	static bool is_initialized = false;

	/* If this is the first call, set up the fast lookup table to
	   give direct access to the procedural expansions table for
	   a given character. */
	if (!is_initialized) {
		cw_debug_msg ((&cw_debug_object), CW_DEBUG_LOOKUPS, CW_DEBUG_INFO,
			      "libcw: initialize prosign fast lookup table");

		for (const cw_prosign_entry_t *e = CW_PROSIGN_TABLE; e->character; e++) {
			lookup[(unsigned char) e->character] = e;

			is_initialized = true;
		}
	}

	/* Lookup the procedural signal table entry.  Unknown characters
	   return NULL.  All procedural signals are non-alphabetical, so no
	   need to use any uppercase coercion here. */
	const cw_prosign_entry_t *cw_prosign = lookup[(unsigned char) c];

	if (cw_debug_has_flag((&cw_debug_object), CW_DEBUG_LOOKUPS)) {
		if (cw_prosign) {
			fprintf(stderr, "libcw: prosign lookup '%c' returned <'%c':\"%s\":%d>\n",
				c, cw_prosign->character,
				cw_prosign->expansion, cw_prosign->is_usually_expanded);
		} else if (isprint(c)) {
			fprintf(stderr, "libcw: prosign lookup '%c' found nothing\n", c);
		} else {
			fprintf(stderr, "libcw: prosign lookup 0x%02x found nothing\n",
				(unsigned char) c);
		}
	}

	/* If found, return any display hint and the expansion; otherwise, NULL. */
	if (cw_prosign) {
		*is_usually_expanded = cw_prosign->is_usually_expanded;
		return cw_prosign->expansion;
	} else {
		return NULL;
	}
}





/**
   \brief Get the string expansion of a given Morse code procedural signal character

   On success the function
   - fills \p expansion with the string expansion of a given Morse code
   procedural signal character \p c;
   - sets is_usuall_expanded to true as a display hint for the caller;
   - returns CW_SUCCESS.

   Both \p expansion and \p is_usually_expanded must be allocated and
   managed by caller. They can be NULL, then the function won't
   attempt to use them.

   The length of \p expansion must be at least by one greater than the
   longest expansion held in the procedural signal character lookup
   table, as returned by cw_get_maximum_procedural_expansion_length().

   If procedural signal character \p c cannot be found, the function sets
   errno to ENOENT and returns CW_FAILURE.

   testedin::test_prosign_lookups_internal()

   \param c - character to look up
   \param expansion - output, space to fill with expansion of the character
   \param is_usually_expanded - visual hint

   \return CW_FAILURE on failure (errno is set to ENOENT)
   \return CW_SUCCESS on success
*/
int cw_lookup_procedural_character(char c, char *expansion, int *is_usually_expanded)
{
	bool is_expanded;

	/* Lookup, and if found, return the string and display hint. */
	const char *retval = cw_lookup_procedural_character_internal(c, &is_expanded);
	if (retval) {
		if (expansion) {
			strcpy(expansion, retval);
		}
		if (is_usually_expanded) {
			*is_usually_expanded = is_expanded;
		}
		return CW_SUCCESS;
	}

	/* Failed to find the requested procedural signal character. */
	errno = ENOENT;
	return CW_FAILURE;
}





/* ******************************************************************** */
/*                     Section:Phonetic alphabet                        */
/* ******************************************************************** */





/* Phonetics table.  Not really CW, but it might be handy to have.
   The table contains ITU/NATO phonetics. */
static const char *const CW_PHONETICS[27] = {
	"Alfa",
	"Bravo",
	"Charlie",
	"Delta",
	"Echo",
	"Foxtrot",
	"Golf",
	"Hotel",
	"India",
	"Juliett",
	"Kilo",
	"Lima",
	"Mike",
	"November",
	"Oscar",
	"Papa",
	"Quebec",
	"Romeo",
	"Sierra",
	"Tango",
	"Uniform",
	"Victor",
	"Whiskey",
	"X-ray",
	"Yankee",
	"Zulu",
	NULL /* guard */
};





/**
   \brief Get maximum length of a phonetic

   testedin::test_phonetic_lookups_internal()

   \return the string length of the longest phonetic in the phonetics lookup table
*/
int cw_get_maximum_phonetic_length(void)
{
	static size_t maximum_length = 0;

	if (maximum_length == 0) {
		/* Traverse the main lookup table, finding the longest. */
		for (int phonetic = 0; CW_PHONETICS[phonetic]; phonetic++) {
			size_t length = strlen(CW_PHONETICS[phonetic]);
			if (length > maximum_length) {
				maximum_length = length;
			}
		}
	}

	return (int) maximum_length;
}





/**
   \brief Get the phonetic of a given character

   On success the routine fills in the string pointer passed in with the
   phonetic of given character \p c.

   The length of phonetic must be at least one greater than the longest
   phonetic held in the phonetic lookup table, as returned by
   cw_get_maximum_phonetic_length().

   If character cannot be found, the function sets errno to ENOENT.

   testedin::test_phonetic_lookups_internal()

   \param c - character to look up
   \param phonetic - output, space for phonetic of a character

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
int cw_lookup_phonetic(char c, char *phonetic)
{
	/* Coerce to uppercase, and verify the input argument. */
	c = toupper(c);
	if (c >= 'A' && c <= 'Z') {
		if (phonetic) {
			strcpy(phonetic, CW_PHONETICS[c - 'A']);
			return CW_SUCCESS;
		}
	}

	/* No such phonetic. */
	errno = ENOENT;
	return CW_FAILURE;
}





/**
   \brief Checks that the given character is validly sendable in Morse

   Function sets errno to ENOENT on failure.

   testedin::test_validate_character_and_string_internal()

   \param c - character to check

   \return CW_SUCCESS if character is valid
   \return CW_FAILURE if character is invalid
*/
bool cw_character_is_valid(char c)
{
	/* If the character is the space special-case, or it is in the
	   lookup table, return success. */
	if (c == ' ' || cw_character_to_representation_internal(c)) {
		return CW_SUCCESS;
	} else {
		errno = ENOENT;
		return CW_FAILURE;
	}
}





int cw_check_character(char c)
{
	return (int) cw_character_is_valid(c);
}





/**
   \brief Validate a string

   Check that each character in the given string is valid and can be
   sent by libcw as a Morse character.

   Function sets errno to EINVAL on failure

   testedin::test_validate_character_and_string_internal()

   \param string - string to check

   \return CW_SUCCESS on success
   \return CW_FAILURE on failure
*/
bool cw_string_is_valid(const char *string)
{
	/* Check that each character in the string has a Morse
	   representation, or - as a special case - is a space character. */
	for (int i = 0; string[i] != '\0'; i++) {
		if (!(string[i] == ' '
		      || cw_character_to_representation_internal(string[i]))) {

			errno = EINVAL;
			return CW_FAILURE;
		}
	}

	return CW_SUCCESS;
}





int cw_check_string(const char *string)
{
	return cw_string_is_valid(string);
}






/* Unit tests. */

#ifdef LIBCW_UNIT_TESTS


#include <stdlib.h> /* free() */

#include "libcw_utils.h"



#define REPRESENTATION_LEN 7
/* for maximum length of 7, there should be 254 items:
   2^1 + 2^2 + 2^3 + ... + 2^7 */
#define REPRESENTATION_TABLE_SIZE ((2 << (REPRESENTATION_LEN + 1)) - 1)




/**
   tests::cw_representation_to_hash_internal()

   The function builds every possible valid representation no longer
   than 7 chars, and then calculates a hash of the
   representation. Since a representation is valid, the tested
   function should calculate a valid hash.

   The function does not compare a representation and its hash to
   verify that patterns in representation and in hash match.

   TODO: add code that would compare the patterns of dots/dashes in
   representation against pattern of bits in hash.

   TODO: test calling the function with invalid representation.
*/
unsigned int test_cw_representation_to_hash_internal(void)
{
	int p = fprintf(stdout, "libcw/data: cw_representation_to_hash_internal():");


	/* Intended contents of input[] is something like that:
	  input[0]  = "."
	  input[1]  = "-"
	  input[2]  = ".."
	  input[3]  = "-."
	  input[4]  = ".-"
	  input[5]  = "--"
	  input[6]  = "..."
	  input[7]  = "-.."
	  input[8]  = ".-."
	  input[9]  = "--."
	  input[10] = "..-"
	  input[11] = "-.-"
	  input[12] = ".--"
	  input[13] = "---"
	  .
	  .
	  .
	  input[248] = ".-.----"
	  input[249] = "--.----"
	  input[250] = "..-----"
	  input[251] = "-.-----"
	  input[252] = ".------"
	  input[253] = "-------"
	*/
	char input[REPRESENTATION_TABLE_SIZE][REPRESENTATION_LEN + 1];

	/* build table of all valid representations ("valid" as in "build
	   from dash and dot, no longer than REPRESENTATION_LEN"). */
	long int rep = 0;
	for (unsigned int len = 1; len <= REPRESENTATION_LEN; len++) {

		/* Build representations of all lengths, starting from
		   shortest (single dot or dash) and ending with the
		   longest representations. */

		unsigned int bit_vector_len = 2 << (len - 1);

		/* A representation of length "len" can have 2^len
		   distinct values. The "for" loop that we are in
		   iterates over these 2^len forms. */
		for (unsigned int bit_vector = 0; bit_vector < bit_vector_len; bit_vector++) {

			/* Turn every '0' into dot, and every '1' into dash. */
			for (unsigned int bit_pos = 0; bit_pos < len; bit_pos++) {
				unsigned int bit = bit_vector & (1 << bit_pos);
				input[rep][bit_pos] = bit ? '-' : '.';
				// fprintf(stderr, "rep = %x, bit pos = %d, bit = %d\n", bit_vector, bit_pos, bit);
			}

			input[rep][len] = '\0';
			//fprintf(stderr, "\ninput[%ld] = \"%s\"", rep, input[rep]);
			rep++;
		}
	}

	/* compute hash for every valid representation */
	for (int i = 0; i < rep; i++) {
		unsigned int hash = cw_representation_to_hash_internal(input[i]);
		/* The function returns values in range 2 - 255. */
		cw_assert (hash >= 2 && hash <= 255, "Invalid hash #%d: %u\n", i, hash)
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   tests::cw_representation_to_character_internal()
*/
unsigned int test_cw_representation_to_character_internal(void)
{
	int p = fprintf(stdout, "libcw/data: cw_representation_to_character_internal():");

	/* The test is performed by comparing results of function
	   using fast lookup table, and function using direct
	   lookup. */

	for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {

		int lookup = cw_representation_to_character_internal(cw_entry->representation);
		int direct = cw_representation_to_character_direct_internal(cw_entry->representation);

		cw_assert (lookup == direct, "Failed for \"%s\"\n", cw_entry->representation);
	}

	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}






unsigned int test_cw_representation_to_character_internal_speed(void)
{
	int p = fprintf(stdout, "libcw/data: cw_representation_to_character() speed gain: ");


	/* Testing speed gain between function with direct lookup, and
	   function with fast lookup table.  Test is preformed by
	   running each function N times with timer started before the
	   N runs and stopped after N runs. */

	int N = 1000;

	struct timeval start;
	struct timeval stop;


	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);

	int lookup = cw_timestamp_compare_internal(&start, &stop);



	gettimeofday(&start, NULL);
	for (int i = 0; i < N; i++) {
		for (const cw_entry_t *cw_entry = CW_TABLE; cw_entry->character; cw_entry++) {
			__attribute__((unused)) int rv = cw_representation_to_character_direct_internal(cw_entry->representation);
		}
	}
	gettimeofday(&stop, NULL);

	int direct = cw_timestamp_compare_internal(&start, &stop);

	p += fprintf(stdout, "%.2f", 1.0 * direct / lookup);


	CW_TEST_PRINT_TEST_RESULT(false, p);

	return 0;
}





/**
   \brief Test functions looking up characters and their representation.

   tests::cw_get_character_count()
   tests::cw_list_characters()
   tests::cw_get_maximum_representation_length()
   tests::cw_character_to_representation()
   tests::cw_representation_to_character()
*/
unsigned int test_character_lookups_internal(void)
{
	int count = 0; /* Number of characters. */

	/* Test: get number of characters known to libcw. */
	{
		/* libcw doesn't define a constant describing the
		   number of known/supported/recognized characters,
		   but there is a function calculating the number. One
		   thing is certain: the number is larger than
		   zero. */
		count = cw_get_character_count();
		cw_assert (count > 0, "Invalid number of characters: %d\n", count);

		int n = printf("libcw/data: cw_get_character_count(): %d:", count);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}


	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		/* Of course length of the list must match the
		   character count discovered above. */

		cw_list_characters(charlist);
		printf("libcw/data: cw_list_characters():\n"
		       "libcw/data: %s\n", charlist);
		size_t len = strlen(charlist);
		cw_assert (count == (int) len,
			   "Number of characters don't match: %d != %zd\n",
			   count, len);

		int n = printf("libcw/data: character count is correct (%d == %d)", count, (int) len);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	/* Test: get maximum length of a representation (a string of dots/dashes). */
	{
		/* This test is rather not related to any other, but
		   since we are doing tests of other functions related
		   to representations, let's do this as well. */

		int rep_len = cw_get_maximum_representation_length();
		cw_assert (rep_len > 0, "Maximum representation length invalid: %d\n", rep_len);

		int n = printf("libcw/data: cw_get_maximum_representation_length(): %d:", rep_len);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	/* Test: character <--> representation lookup. */
	{
		/* For each character, look up its representation, the
		   look up each representation in the opposite
		   direction. */

		for (int i = 0; charlist[i] != '\0'; i++) {

			char *representation = cw_character_to_representation(charlist[i]);
			cw_assert (representation, "Failed to get a representation string (#%d)\n", i);


			/* Here we convert the representation into an output char 'c'. */
			char c = cw_representation_to_character(representation);
			cw_assert (c, "Failed to get character for a representation (#%d)\n", i);


			/* Compare output char with input char. */
			cw_assert (charlist[i] == c, "Two-way lookup failure for char '%c' (#%d)\n", c, i);

			free(representation);
			representation = NULL;
		}

		int n = printf("libcw/data: cw_character_to_representation():");
		CW_TEST_PRINT_TEST_RESULT (false, n);


		n = printf("libcw/data: cw_representation_to_character():");
		CW_TEST_PRINT_TEST_RESULT (false, n);


		n = printf("libcw/data: two-way lookup for character lookups:");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}

	return 0;
}





/**
   \brief Test functions looking up procedural characters and their representation.

   tests::cw_get_procedural_character_count()
   tests::cw_list_procedural_characters()
   tests::cw_get_maximum_procedural_expansion_length()
   tests::cw_lookup_procedural_character()
*/
unsigned int test_prosign_lookups_internal(void)
{
	/* Collect and print out a list of characters in the
	   procedural signals expansion table. */

	int count = 0; /* Number of prosigns. */

	/* Test: get number of prosigns known to libcw. */
	{
		count = cw_get_procedural_character_count();
		cw_assert (count > 0, "Invalid number of procedural characters: %d\n", count);

		int n = printf("libcw/data: cw_get_procedural_character_count(): %d", count);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	char charlist[UCHAR_MAX + 1];
	/* Test: get list of characters supported by libcw. */
	{
		cw_list_procedural_characters(charlist);
		printf("libcw/data: cw_list_procedural_characters():\n"
		       "libcw/data: %s\n", charlist);
		size_t len = strlen(charlist);
		cw_assert (count == (int) len,
			   "Number of characters don't match: %d != %zd\n",
			   count, len);

		int n = printf("libcw/data: procedural character count is correct (%d == %d)", count, (int) len);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	/* Test: expansion length. */
	{
		int exp_len = cw_get_maximum_procedural_expansion_length();
		cw_assert (exp_len > 0,
			   "Expansion length invalid: %d\n", exp_len);

		int n = printf("libcw/data: cw_get_maximum_procedural_expansion_length(): %d", exp_len);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	/* Test: lookup. */
	{
		/* For each procedural character, look up its
		   expansion and check for two or three characters,
		   and a true/false assignment to the display hint. */

		for (int i = 0; charlist[i] != '\0'; i++) {
			char expansion[256];
			int is_usually_expanded = -1;

			cw_assert (cw_lookup_procedural_character(charlist[i],
								  expansion,
								  &is_usually_expanded),
				   "Lookup of character '%c' (#%d) failed", charlist[i], i);


			/* TODO: comment, please. */
			if ((strlen(expansion) != 2 && strlen(expansion) != 3)
			    || is_usually_expanded == -1) {

				cw_assert (0, "Expansion check failed\n");
			}
		}

		int n = printf("libcw/data: cw_lookup_procedural_character():");
		CW_TEST_PRINT_TEST_RESULT (false, n);

		n = printf("libcw/data: cw_lookup_procedural_() mapping:");
		CW_TEST_PRINT_TEST_RESULT (false, n);

	}

	return 0;
}





/**
   tests::cw_get_maximum_phonetic_length()
   tests::cw_lookup_phonetic()
*/
unsigned int test_phonetic_lookups_internal(void)
{
	/* For each ASCII character, look up its phonetic and check
	   for a string that start with this character, if alphabetic,
	   and false otherwise. */

	/* Test: check that maximum phonetic length is larger than
	   zero. */
	{
		int len = cw_get_maximum_phonetic_length();
		cw_assert (len > 0, "Maximum phonetic length invalid: %d\n", len);

		int n = printf("libcw/data: cw_get_maximum_phonetic_length(): %d", len);
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}


	/* Test: lookup of phonetic + reverse lookup. */
	{
		for (int i = 0; i < UCHAR_MAX; i++) {
			char phonetic[256];

			int status = cw_lookup_phonetic((char) i, phonetic);
			cw_assert (status == (bool) isalpha(i),
				   "Lookup of phonetic '%c' (#%d) failed\n", (char ) i, i);

			if (status && (bool) isalpha(i)) {
				/* We have looked up a letter, it has
				   a phonetic.  Almost by definition,
				   the first letter of phonetic should
				   be the same as the looked up
				   letter. */
				cw_assert (phonetic[0] == toupper((char) i),
					   "Reverse lookup failed for phonetic \"%s\" ('%c' / #%d)\n",
					   phonetic, (char) i, i);
			}
		}

		int n = printf("libcw/data: cw_lookup_phonetic():");
		CW_TEST_PRINT_TEST_RESULT (false, n);

		n = printf("libcw/data: reverse lookup for phonetic characters:");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}

	return 0;
}





/**
   Validate all supported characters, first each characters individually, then as a string.

   tests::cw_character_is_valid()
   tests::cw_string_is_valid()
*/
unsigned int test_validate_character_and_string_internal(void)
{
	/* Test: validation of individual characters. */
	{
		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);

		for (int i = 0; i < UCHAR_MAX; i++) {
			if (i == ' '
			    || (i != 0 && strchr(charlist, toupper(i)) != NULL)) {

				/* Here we have a valid character, that is
				   recognized/supported as 'sendable' by
				   libcw.  cw_character_is_valid() should
				   confirm it. */
				cw_assert (cw_character_is_valid(i),
					   "Valid character '%c' / #%d not recognized as valid\n",
					   (char ) i, i);
			} else {
				/* The 'i' character is not
				   recognized/supported by libcw.
				   cw_character_is_valid() should return false
				   to signify that the char is invalid. */
				cw_assert (!cw_character_is_valid(i),
					   "Invalid character '%c' / #%d recognized as valid\n",
					   (char ) i, i);
			}
		}

		int n = printf("libcw/data: cw_character_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);

		n = printf("libcw/data: cw_character_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}



	/* Test: validation of string as a whole. */
	{
		/* Check the whole charlist item as a single string,
		   then check a known invalid string. */

		char charlist[UCHAR_MAX + 1];
		cw_list_characters(charlist);
		cw_assert (cw_string_is_valid(charlist),
			   "String with valid characters not recognized as valid\n");

		int n = printf("libcw/data: cw_string_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);

		/* Test invalid string. */
		cw_assert (!cw_string_is_valid("%INVALID%"),
			   "String with invalid characters recognized as valid\n");

		n = printf("libcw/data: cw_string_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}


	return 0;
}





/**
   \brief Validating representations of characters

   tests::cw_representation_is_valid()
*/
unsigned int test_validate_representation_internal(void)
{
	/* Test: validating valid representations. */
	{
		cw_assert (cw_representation_is_valid(".-.-.-"), "Valid representation #1 not recognized as valid.\n");
		cw_assert (cw_representation_is_valid(".-"),     "Valid representation #2 not recognized as valid.\n");
		cw_assert (cw_representation_is_valid("---"),    "Valid representation #3 not recognized as valid.\n");
		cw_assert (cw_representation_is_valid("...-"),   "Valid representation #4 not recognized as valid.\n");

		int n = printf("libcw/data: cw_representation_is_valid(<valid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}


	/* Test: validating invalid representations. */
	{
		cw_assert (!cw_representation_is_valid("INVALID"), "Invalid representation #1 not recognized as invalid.\n");
		cw_assert (!cw_representation_is_valid("_._"),     "Invalid representation #2 not recognized as invalid.\n");
		cw_assert (!cw_representation_is_valid("-_-"),     "Invalid representation #3 not recognized as invalid.\n");

		int n = printf("libcw/data: cw_representation_is_valid(<invalid>):");
		CW_TEST_PRINT_TEST_RESULT (false, n);
	}

	return 0;
}





#endif /* #ifdef LIBCW_UNIT_TESTS */
