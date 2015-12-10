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

#ifndef H_CW_DICTIONARY
#define H_CW_DICTIONARY

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdbool.h>


typedef struct cw_dictionary_s cw_dictionary_t;

extern bool cw_dictionaries_read(const char *file);
extern void cw_dictionaries_unload(void);
extern bool cw_dictionaries_write(const char *file);

extern const cw_dictionary_t *cw_dictionaries_iterate(const cw_dictionary_t *dict);

extern const char *cw_dictionary_get_description(const cw_dictionary_t *dict);
extern int         cw_dictionary_get_group_size(const cw_dictionary_t *dict);
extern const char *cw_dictionary_get_random_word(const cw_dictionary_t *dict);



/* Everything below is deprecated. */
typedef struct cw_dictionary_s dictionary;

extern int  dictionary_load(const char *file)  __attribute__ ((deprecated));  /* Use cw_dictionaries_read() */
extern void dictionary_unload(void)            __attribute__ ((deprecated));  /* Use cw_dictionaries_unload() */
extern int  dictionary_write(const char *file) __attribute__ ((deprecated));  /* Use cw_dictionaries_write() */

extern const dictionary *dictionary_iterate(const dictionary *dict) __attribute__ ((deprecated));   /* Use cw_dictionary_iterate() */

extern const char *get_dictionary_description(const dictionary *dict) __attribute__ ((deprecated)); /* Use cw_dictionary_get_description() */
extern int         get_dictionary_group_size(const dictionary *dict)  __attribute__ ((deprecated)); /* Use cw_dictionary_get_group_size() */
extern const char *get_dictionary_random_word(const dictionary *dict) __attribute__ ((deprecated)); /* Use cw_dictionary_get_random_word() */

#if defined(__cplusplus)
}
#endif
#endif  /* H_CW_DICTIONARY */
