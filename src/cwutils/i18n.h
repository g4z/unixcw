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

#ifndef _CWI18N_H
#define _CWI18N_H

#if defined(HAVE_LIBINTL_H)
# include <libintl.h>

# define _(STR) i18n_gettext (STR)
# define gettext_noop(STR) (STR)
# define N_(STR) gettext_noop (STR)

#else

# define _(STR) (STR)
# define N_(STR) (STR)

#endif

#if defined(__cplusplus)
extern "C" {
#endif

extern void i18n_initialize (void);
extern const char *i18n_gettext (const char *msgid);

#if defined(__cplusplus)
}
#endif
#endif  /* _CWI18N_H */
