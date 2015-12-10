/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_CONSOLE
#define H_LIBCW_CONSOLE





#include "libcw_gen.h"
#include "libcw_tq.h"

int  cw_console_configure(cw_gen_t *gen, const char *device);
int  cw_console_write(cw_gen_t *gen, cw_tone_t *tone);
void cw_console_silence(cw_gen_t *gen);





#endif /* #ifndef H_LIBCW_CONSOLE */
