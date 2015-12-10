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

#ifndef _CWCMDLINE_H
#define _CWCMDLINE_H

#include <stdbool.h>
#include "cw_common.h" /* cw_config_t */

#if defined(__cplusplus)
extern "C" {
#endif


extern const char *cw_program_basename(const char *argv0);
extern void combine_arguments(const char *env_variable,
			      int argc, char *const argv[],
			      int *new_argc, char **new_argv[]);

extern int cw_process_argv(int argc, char *const argv[], const char *options, cw_config_t *config);
extern bool has_longopts(void);
extern int get_option(int argc, char *const argv[],
                       const char *descriptor,
                       int *option, char **argument);
extern int get_optind(void);

#if defined(__cplusplus)
}
#endif
#endif  /* _CWCMDLINE_H */
