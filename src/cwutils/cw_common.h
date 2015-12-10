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


#ifndef H_CW_COMMON
#define H_CW_COMMON

#if defined(__cplusplus)
extern "C" {
#endif


#include <stdbool.h>
#include <stdio.h>


#define CW_PRACTICE_TIME_MIN        1
#define CW_PRACTICE_TIME_MAX       99
#define CW_PRACTICE_TIME_INITIAL   15
#define CW_PRACTICE_TIME_STEP       1


typedef struct {
	char *program_name;
	int audio_system;
	char *audio_device;
	int send_speed;
	int frequency;
	int volume;
	int gap;
	int weighting;
	int practice_time;
	char *input_file;
	char *output_file;

	int is_cw;
	int has_practice_time;
	int has_outfile;
	bool has_infile;

	/*
	 * Program-specific state variables, settable from the command line, or from
	 * embedded input stream commands.  These options may be set by the embedded
	 * command parser to values other than strictly TRUE or FALSE; all non-zero
	 * values are equivalent to TRUE.
	 *
	 * These fields are used only in cw.
	 */
	int do_echo;           /* Echo characters */
        int do_errors;         /* Print error messages to stderr */
        int do_commands;       /* Execute embedded commands */
        int do_combinations;   /* Execute [...] combinations */
        int do_comments;       /* Allow {...} as comments */
} cw_config_t;



extern void cw_print_help(cw_config_t *config);

extern cw_config_t *cw_config_new(const char *program_name);
extern void         cw_config_delete(cw_config_t **config);
extern int          cw_config_is_valid(cw_config_t *config);

extern int cw_generator_new_from_config(cw_config_t *config);

extern void cw_start_beep(void);
extern void cw_end_beep(void);
extern bool cw_getline(FILE *stream, char *buffer, int buffer_size);


#if defined(__cplusplus)
}
#endif


#endif /* H_CW_COMMON */
