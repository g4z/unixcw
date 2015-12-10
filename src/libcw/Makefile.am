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
lib_LTLIBRARIES=libcw.la
check_PROGRAMS=libcw_test_public libcw_test_internal libcw_test_simple_gen
man_MANS=libcw.3 cw.7
pkgconfig_DATA=libcw.pc


EXTRA_DIST=include.awk libdoc.awk libfuncs.awk libpc.awk libsigs.awk \
	libcw.3.m4 \
	libcw.pc.in \
	cw.7 \
	libcw_gen.h libcw_rec.h \
	libcw_tq.h libcw_data.h libcw_key.h libcw_utils.h libcw_signal.h \
	libcw_null.h libcw_console.h libcw_oss.h libcw_alsa.h libcw_pa.h \
	libcw_test.h

# These files are used to build two different targets - list them only
# once. I can't compile these files into an utility library because
# the two targets are compiled with different CPPFLAGS.
LIBCW_BASE_C_FILES = \
	libcw.c \
	libcw_gen.c libcw_rec.c \
	libcw_tq.c libcw_data.c libcw_key.c libcw_utils.c libcw_signal.c \
	libcw_null.c libcw_console.c libcw_oss.c libcw_alsa.c libcw_pa.c \
	libcw_debug.c





# target: shared library

# source code files used to build libcw shared library
libcw_la_SOURCES = $(LIBCW_BASE_C_FILES)

# target-specific linker flags (objects to link)
libcw_la_LIBADD=-lm -lpthread $(DL_LIB) $(OSS_LIB)

# target-specific linker flags (additional flags)
# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
# LIBCW_VERSION is defined in configure.ac
libcw_la_LDFLAGS = -version-number $(LIBCW_VERSION)

# target-specific compiler flags
libcw_la_CFLAGS = -rdynamic


# target-specific preprocessor flags (#defs and include dirs)
#
# $(LIBCW_NDEBUG) activates asserts in base libcw for dev builds, and
# deactivates asserts in base libcw for regular builds.
libcw_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBCW_NDEBUG)


libcw_includedir=$(includedir)
libcw_include_HEADERS = libcw.h libcw_debug.h





# target: libcw_test_public: a test program testing only public API of libcw library;
# main() is defined in libcw_test_public.c
# Source code files used to build libcw_test_public program.
# This test tests library's public API, so the target should depend on
# a regular shared library file found in build directory. It shouldn't
# compile all *.c files that are used to create library, it should
# compile only test files and "link" them with this library file.
libcw_test_public_SOURCES = $(LIBCW_BASE_C_FILES) libcw_test_public.c libcw_test.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_public_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCW_UNIT_TESTS

# target-specific linker flags (objects to link)
libcw_test_public_LDADD = -lm -lpthread $(DL_LIB) -L./.libs -lcw





# target: libcw_test_internal, a test program invoking unit tests for
# (internal) functions defined in libcw_xyz.c files;
# main() is defined in libcw_test_internal.c.

# source code files used to build libcw_test_internal program
libcw_test_internal_SOURCES = $(LIBCW_BASE_C_FILES) libcw_test_internal.c

# target-specific preprocessor flags (#defs and include dirs)
libcw_test_internal_CPPFLAGS = $(AM_CPPFLAGS) -DLIBCW_UNIT_TESTS

# target-specific linker flags (objects to link)
libcw_test_internal_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB)

# target-specific compiler flags
libcw_test_internal_CFLAGS = -rdynamic





# target: libcw_test_simple_gen, yet another test program;
# main() is defined in libcw_test_simple_gen.c
# The file defines a very simple set of tests that make only a basic
# use of libcw's generator facilities.

# source code files used to build libcw_test_simple_gen program
libcw_test_simple_gen_SOURCES = libcw_test_simple_gen.c libcw_test.c

# target-specific linker flags (objects to link)
libcw_test_simple_gen_LDADD=-lm -lpthread $(DL_LIB) $(OSS_LIB) -L./.libs -lcw

# target-specific compiler flags
libcw_test_simple_gen_CFLAGS = -rdynamic





# target: libcw man page
libcw.3: libcw.3.m4
	cat $(top_srcdir)/src/libcw/*.c | $(AC_AWK) -f $(top_srcdir)/src/libcw/libdoc.awk | $(AC_AWK) -f $(top_srcdir)/src/libcw/libsigs.awk  > signatures
	cat $(top_srcdir)/src/libcw/*.c | $(AC_AWK) -f $(top_srcdir)/src/libcw/libdoc.awk | $(AC_AWK) -f $(top_srcdir)/src/libcw/libfuncs.awk > functions
	$(AC_AWK) -f $(top_srcdir)/src/libcw/include.awk < $(top_srcdir)/src/libcw/libcw.3.m4 >libcw.3
	rm -f signatures functions





# target: libcw.pc
pkgconfigdir = $(libdir)/pkgconfig




# CLEANFILES extends list of files that need to be removed when
# calling "make clean"
CLEANFILES = libcw_test_internal.sh libcw.3




TESTS = $(check_SCRIPTS)

# run test programs (only libcw_test_internal unit tests suite)
check_SCRIPTS = libcw_test_internal.sh
libcw_test_internal.sh:
	echo './libcw_test_internal | grep "test result: success"' > libcw_test_internal.sh
	chmod +x libcw_test_internal.sh


# sources, references
#
# source of snippet related to "check_SCRIPTS" and related sh script:
# http://www.freesoftwaremagazine.com/books/agaal/automatically_writing_makefiles_with_autotools
