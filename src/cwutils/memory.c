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

#include <stdlib.h>
#include <stdio.h>

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif

#include "memory.h"


/*---------------------------------------------------------------------*/
/*  Memory allocation wrappers                                         */
/*---------------------------------------------------------------------*/

/*
 * safe_malloc()
 * safe_realloc()
 * safe_strdup()
 *
 * Non-failing wrappers around malloc(), realloc(), and strdup().
 */
void *
safe_malloc (size_t size)
{
  void *pointer;

  pointer = malloc (size);
  if (!pointer)
    {
      perror ("malloc");
      exit (EXIT_FAILURE);
    }

  return pointer;
}

void *
safe_realloc (void *ptr, size_t size)
{
  void *pointer;

  pointer = realloc (ptr, size);
  if (!pointer)
    {
      perror ("realloc");
      exit (EXIT_FAILURE);
    }

  return pointer;
}

char *
safe_strdup (const char *s)
{
  char *copy;

  copy = malloc (strlen (s) + 1);
  if (!copy)
    {
      perror ("strdup");
      exit (EXIT_FAILURE);
    }

  strcpy (copy, s);
  return copy;
}
