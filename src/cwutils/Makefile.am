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

# targets to be built in this directory
check_PROGRAMS=cw_dictionary_tests


# source code files used to build cw_dictionary_tests program
cw_dictionary_tests_SOURCES = dictionary.c memory.c i18n.c cw_common.c

# target-specific preprocessor flags (#defs and include dirs)
cw_dictionary_tests_CPPFLAGS = $(AM_CPPFLAGS) -DCW_DICTIONARY_UNIT_TESTS

# target-specific linker flags (objects to link)
cw_dictionary_tests_LDADD=-L$(top_builddir)/src/libcw/.libs -lcw #-lm -lpthread -ldl

# target-specific compiler flags
cw_dictionary_tests_CFLAGS = -rdynamic



# no header from this dir should be installed
# noinst_HEADERS = cmdline.h cw_copyright.h cw_common.h cw_words.h dictionary.h i18n.h memory.h

# convenience libraries
noinst_LIBRARIES = lib_cw.a lib_cwcp.a lib_cwgen.a lib_xcwcp.a

lib_cw_a_SOURCES    = cw_copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h
lib_cwcp_a_SOURCES  = cw_copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h dictionary.c dictionary.h cw_words.h
lib_cwgen_a_SOURCES = cw_copyright.h i18n.c i18n.h                         cmdline.c cmdline.h memory.c memory.h
lib_xcwcp_a_SOURCES = cw_copyright.h i18n.c i18n.h cw_common.c cw_common.h cmdline.c cmdline.h memory.c memory.h dictionary.c dictionary.h cw_words.h


# Test targets; no self-test, but make sure all is built.
#check: all


# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = greptest.sh



TESTS = $(check_SCRIPTS)

# run test programs (only libcwunittests unit tests suite)
check_SCRIPTS = greptest.sh
greptest.sh:
	echo './cw_dictionary_tests | grep "test result: success"' > greptest.sh
	chmod +x greptest.sh
