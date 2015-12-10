#!/bin/awk -f
#
# Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
# Copyright (C) 2011-2015  Kamil Ignacak (acerion@wp.pl)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
#
# AWK script to replicate just the include() function of m4.  This avoids
# the need for a full m4 binary; overkill for what we really need.
#

# Catch 'include(...)' special lines; watch out, mawk doesn't support
# POSIX character classes (http://ubuntuforums.org/archive/index.php/t-619985.html)
/^include\([^\)]*\)$/ {


  # Find the name of the file being included.
  file = $0
  sub(/^include\(/, "", file)
  sub(/\)$/, "", file)

  # Read in each line of the file, and print it out.
  while ((status=getline line <file) > 0)
    print line
  close (file)

  # Check for read or general getline error.
  if (status != 0)
    {
      printf ("Error reading file %s: %s\n", file, ERRNO) | "cat 1>&2"
      exit 1
    }
  next
}

# Pass unchanged all lines that don't look like 'include(...)'
{
  print $0
}
