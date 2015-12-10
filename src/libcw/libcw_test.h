/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_TEST
#define H_LIBCW_TEST





#include <stddef.h> /* size_t */





/* Total width of test name + test status printed in console. Remember
   that some consoles have width = 80. Not everyone works in X. */
static const int cw_test_print_width = 75;


/* Notice that failure status string ("FAIL!") is visually very
   different than "success". This makes finding failed tests
   easier. */
#define CW_TEST_PRINT_TEST_RESULT(m_failure, m_n) {			\
		printf("%*s\n", (cw_test_print_width - m_n), m_failure ? " FAIL! " : "success"); \
	}

#define CW_TEST_PRINT_FUNCTION_COMPLETED(m_func_name) {			\
		int m = printf("libcw: %s(): ", m_func_name);		\
		printf("%*s\n\n", cw_test_print_width - m, "completed");	\
	}


int cw_test_args(int argc, char *const argv[], char *sound_systems, size_t systems_max, char *modules, size_t modules_max);
void cw_test_print_help(const char *progname);





#endif /* #ifndef H_LIBCW_TEST */
