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

#ifndef _CW_H
#define _CW_H

#if defined(__cplusplus)
extern "C" {
#endif

/* General command and query introducers. */
enum
{ CW_CMD_ESCAPE  = '%',  /* Command escape character */
  CW_CMD_QUERY   = '?',  /* Query subcommand */
  CW_CMD_CWQUERY = '>',  /* CW report query subcommand */
  CW_CMD_END     = ';'   /* Completes an embedded command */
};

/* Specific command value specifiers. */
enum
{ CW_CMDV_FREQUENCY    = 'T',  /* CW tone */
  CW_CMDV_VOLUME       = 'V',  /* CW volume */
  CW_CMDV_SPEED        = 'W',  /* CW words-per-minute */
  CW_CMDV_GAP          = 'G',  /* CW extra gaps in sending */
  CW_CMDV_WEIGHTING    = 'K',  /* CW weighting */
  CW_CMDV_ECHO         = 'E',  /* CW echo chars to stdout */
  CW_CMDV_ERRORS       = 'M',  /* CW error msgs to stderr */
  CW_CMDV_SOUND        = 'S',  /* CW creates tones. otherwise silent */
  CW_CMDV_COMMANDS     = 'C',  /* CW responds to @... embedded cmds */
  CW_CMDV_COMBINATIONS = 'O',  /* CW allows [..] combinations */
  CW_CMDV_COMMENTS     = 'P',  /* CW allows {..} combinations */
  CW_CMDV_QUIT         = 'Q'   /* CW program exit command */
};

/* Combination and comment start and end characters. */
enum
{ CW_COMBINATION_START = '[',  /* Begin [..] */
  CW_COMBINATION_END   = ']',  /* End [..] */
  CW_COMMENT_START     = '{',  /* Begin {..} */
  CW_COMMENT_END       = '}'   /* End {..} */
};

/* Status values - first character of stderr messages. */
enum
{ CW_STATUS_OK  = '=',  /* =... command accepted */
  CW_STATUS_ERR = '?'   /* ?... error in command */
};

#if defined(__cplusplus)
}
#endif
#endif  /* _CW_H */
