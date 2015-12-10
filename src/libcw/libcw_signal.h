/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_SIGNAL
#define H_LIBCW_SIGNAL





#include <stdbool.h>





int  cw_sigalrm_install_top_level_handler_internal(void);
bool cw_sigalrm_is_blocked_internal(void);
int  cw_signal_wait_internal(void);
int  cw_sigalrm_restore_internal(void);
int  cw_timer_run_with_handler_internal(int usecs, void (*sigalrm_handler)(void));




#endif /* #ifndef H_LIBCW_SIGNAL */
