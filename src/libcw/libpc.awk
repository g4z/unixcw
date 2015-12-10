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
# AWK script to produce a pkg-config metadata file.
#

# If we're building under RPM, we need to strip the RPM_BUILD_ROOT from the
# start of each of the prefixed items we use, passed in as strip.
function adjust(path) {
  if (length (strip) > 0)
    {
      if (index (path, strip) == 1)
        return substr (path, length (strip) + 1)
    }
  return path
}


# Extract the package version from config.h, passed in as config_h.
function version() {
  while ((status=getline line <config_h) > 0)
    {
      if (line ~ /\#define PACKAGE_VERSION ".*"/)
        {
          split (line, pieces)
          retval = pieces[3]
          gsub (/"/, "", retval)
          close (config_h)
          return retval
        }
    }
  return "[unknown]"
}


BEGIN {
  # Start with prefix preamble.
  printf ("prefix=%s\n", adjust(prefix))
  printf ("exec_prefix=%s\n", adjust(exec_prefix))
  printf ("libdir=%s\n", adjust(libdir))
  printf ("includedir=%s\n\n", adjust(includedir))


  # Print the remaining metadata.
  printf ("Name: libcw\nDescription: CW (Morse code) library.\n")
  printf ("Version: %s\nRequires: alsa\n", version())
  printf ("Libs: -L${libdir} -lcw -lpthread -lm\nCflags: -I${includedir}\n")
}
