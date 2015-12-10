/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_DATA
#define H_LIBCW_DATA





#include <stdbool.h>





typedef struct cw_entry_struct{
	const char character;              /* Character represented */
	const char *const representation;  /* Dot-dash shape of the character */
} cw_entry_t;





/* functions handling representation of a character;
   representation looks like this: ".-" for "a", "--.." for "z", etc. */
int          cw_representation_lookup_init_internal(const cw_entry_t *lookup[]);
int          cw_representation_to_character_internal(const char *representation);
__attribute__((unused)) int cw_representation_to_character_direct_internal(const char *representation);
unsigned int cw_representation_to_hash_internal(const char *representation);
const char  *cw_character_to_representation_internal(int c);
const char  *cw_lookup_procedural_character_internal(int c, bool *is_usually_expanded);





#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_representation_to_hash_internal(void);
unsigned int test_cw_representation_to_character_internal(void);
unsigned int test_cw_representation_to_character_internal_speed(void);
unsigned int test_character_lookups_internal(void);
unsigned int test_prosign_lookups_internal(void);
unsigned int test_phonetic_lookups_internal(void);
unsigned int test_validate_character_and_string_internal(void);
unsigned int test_validate_representation_internal(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_DATA */
