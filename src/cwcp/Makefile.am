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

-include $(top_builddir)/Makefile.inc

# program(s) to be built in current dir
bin_PROGRAMS = cwcp

# source code files used to build cwcp program
cwcp_SOURCES = cwcp.c
# target-specific preprocessor flags (#defs and include dirs)
#cwcp_CPPFLAGS = -I$(top_srcdir)/src/cwutils/ -I$(top_srcdir)/src/libcw/
# target-specific linker flags (objects to link)
cwcp_LDADD = -lcurses -L$(top_builddir)/src/libcw/.libs -lcw $(top_builddir)/src/cwutils/lib_cwcp.a


# copy man page to proper directory during installation
man_MANS = cwcp.1
# and mark it as distributable, too
EXTRA_DIST = cwcp.1


# Test targets.
# This test target will be invoked when creating deb package.
check:


# This test target can be invoked manually.
real_check: all
	( echo "Trying with OSS audio output";				\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -so ) ||	\
	( echo "Retrying with ALSA audio output";			\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -sa ) ||	\
	( echo "Retrying with console audio output";			\
	  LIBCW_DEBUG=1 						\
	  LD_LIBRARY_PATH=../libcw:$$LD_LIBRARY_PATH ./cwcp -sc )

