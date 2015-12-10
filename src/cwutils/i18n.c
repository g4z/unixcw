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

#include "config.h"

#include <locale.h>

#include "i18n.h"


/*---------------------------------------------------------------------*/
/*  Internationalization                                               */
/*---------------------------------------------------------------------*/

enum { FALSE = 0, TRUE = !FALSE };

/*
 * i18n_initialize()
 *
 * Set locale, and message locations if available.
 */
void
i18n_initialize (void)
{
  setlocale (LC_ALL, "");
}


/*
 * i18n_gettext()
 *
 * Wrapper for gettext().  This function is the destination for _("mumble").
 */
const char *
i18n_gettext (const char *msgid)
{
#if defined(HAVE_LIBINTL_H)
  static int is_initialized = FALSE;

  if (!is_initialized)
    {
      textdomain (PACKAGE_NAME);

      is_initialized = TRUE;
    }

  return gettext (msgid);
#else
  return msgid;
#endif
}
