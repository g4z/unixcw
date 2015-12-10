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

# General initializations.
AC_PREREQ(2.57)

AC_INIT([unixcw], [3.4.2])
AC_CONFIG_SRCDIR([src/libcw/libcw_gen.c])
AM_INIT_AUTOMAKE

# Libtool initialization, added during tests on FreeBSD
LT_INIT

# Specify a configuration header.
AC_CONFIG_HEADERS(src/config.h)

AC_CONFIG_FILES([src/libcw/libcw.pc])

# General checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_LIBTOOL
AC_PROG_RANLIB # Added during tests on FreeBSD
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_LN_S
AM_PROG_CC_C_O





# OS-specific configuration. On top there are default values of some
# flags, to be modified per host type.

# General notice: FreeBSD 9.3 doesn't like empty quotes
# (e.g. DL_LIB=\"\"). Empty string should be totally empty.

# libcw on Linux needs "-ldl" library (dlopen() and related symbols)
# for dynamic loading of ALSA and PulseAudio libraries. Tested BSD
# systems (FreeBSD and OpenBSD) don't have a separate -ldl library
# (and since they don't support ALSA and PulseAudio, libcw on BSDs
# wouldn't even need the library).
DL_LIB=

# libcw on OpenBSD (tested on 5.5) needs -lossaudio for _oss_ioctl
# symbol. No explicit OSS library needed by libcw on FreeBSD and
# Linux. Quick and short googling suggests that NetBSD also uses
# ossaudio library.
# TODO: consider using AC_CHECK_LIB(ossaudio, _oss_ioctl)
OSS_LIB=

AC_CANONICAL_HOST
case $host_os in
    *linux*|*Linux*)
	AC_MSG_NOTICE([Detected Linux: $host_os])
	# Needed because of dynamic loading of libraries in libcw.
	DL_LIB=\"-ldl\"
	;;
    *freebsd*|*FreeBSD*)
	AC_MSG_NOTICE([Detected FreeBSD: $host_os])
	;;
    *openbsd*|*OpenBSD*)
	AC_MSG_NOTICE([Detected OpenBSD: $host_os])
	OSS_LIB=\"-lossaudio\"
	;;
    *netbsd*|*NetBSD*)
	# I didn't test unixcw on NetBSD, but let's give it a
	# chance.
	AC_MSG_NOTICE([Detected NetBSD: $host_os])
	OSS_LIB=\"-lossaudio\"
	;;
    *)
	AC_MSG_NOTICE([Detected other OS: $host_os])
	# Until someone complains, for other OSes we will configure
	# the flags the same way as on Linux.
	DL_LIB=\"-ldl\"
	;;
esac

AC_SUBST(DL_LIB)
AC_SUBST(OSS_LIB)




# Check specifically for gzip and ldconfig, and substitute the harmless ":"
# if absent.
AC_PATH_PROG(GZIP, gzip, ,)
if test -z "$GZIP" ; then
  GZIP=":"
fi
AC_PATH_PROG(LDCONFIG, ldconfig, ,)
if test -z "$LDCONFIG" ; then
  LDCONFIG=":"
fi
AC_SUBST(GZIP)
AC_SUBST(LDCONFIG)




# Check for basic but essential libraries
AC_CHECK_LIB(pthread, pthread_create)
AC_CHECK_LIB(m, floor)

# show-stoppers
if test $ac_cv_lib_m_floor = 'no' ; then
   AC_MSG_ERROR([math library is missing, cannot continue])
fi
if test $ac_cv_lib_pthread_pthread_create = 'no' ; then
   AC_MSG_ERROR([pthread library is missing, cannot continue])
fi



# http://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
# library's current:revision:age
#
# I'm defining it here so that it's available in config.h and can be
# used in libcw's cw_version() function that should return the
# numbers.
LIBCW_VERSION=\"6:4:1\"
AC_DEFINE_UNQUOTED([LIBCW_VERSION], $LIBCW_VERSION, [Library version, libtool notation])
AC_SUBST(LIBCW_VERSION)


# ########################################################################
# definitions of custom command line options for configure script
# ########################################################################


# Build support for console buzzer output? Yes by default.
AC_ARG_ENABLE([console],
    AS_HELP_STRING([--disable-console], [disable support for console buzzer audio output]),
    [],
    [enable_console=yes])

AC_MSG_CHECKING([whether to include console buzzer audio support])
if test "$enable_console" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Build support for OSS audio system? Yes by default.
AC_ARG_ENABLE([oss],
    AS_HELP_STRING([--disable-oss], [disable support for OSS audio output]),
    [],
    [enable_oss=yes])

AC_MSG_CHECKING([whether to include OSS audio support])
if test "$enable_oss" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Build support for ALSA audio system? Yes by default.
AC_ARG_ENABLE([alsa],
    AS_HELP_STRING([--disable-alsa], [disable support for ALSA audio output]),
    [],
    [enable_alsa=yes])

AC_MSG_CHECKING([whether to include ALSA audio support])
if test "$enable_alsa" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Build support for PulseAudio audio system? Yes by default.
AC_ARG_ENABLE(pulseaudio,
    AS_HELP_STRING([--disable-pulseaudio], [disable support for PulseAudio audio output]),
    [],
    [enable_pulseaudio=yes])

AC_MSG_CHECKING([whether to include PulseAudio audio support])
if test "$enable_pulseaudio" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Build cwcp? Yes by default.
AC_ARG_ENABLE(cwcp,
    AS_HELP_STRING([--disable-cwcp], [do not build cwcp (application with curses user interface)]),
    [],
    [enable_cwcp=yes])

AC_MSG_CHECKING([whether to build cwcp])
if test "$enable_cwcp" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Build xcwcp? Yes by default.
AC_ARG_ENABLE(xcwcp,
    AS_HELP_STRING([--disable-xcwcp], [do not build xcwcp (application with Qt4 user interface)]),
    [],
    [enable_xcwcp=yes])

AC_MSG_CHECKING([whether to build xcwcp])
if test "$enable_xcwcp" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# Enable development debugging? No by default.
AC_ARG_ENABLE(dev,
    AS_HELP_STRING([--enable-dev], [enable development support (messages/debug code/asserts)]),
    [],
    [enable_dev=no])

AC_MSG_CHECKING([whether to enable development support])
if test "$enable_dev" = "yes" ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi


# #####
#  end
# #####





# ########################################################################
# Process command line options, and if enabled - checking if requirements
# for enabled features are satisfied.
# ########################################################################


if test "$enable_console" = "no" ; then
    WITH_CONSOLE='no'
else
     # Search for header that provides KIOCSOUND
    AC_CHECK_HEADERS([sys/kd.h sys/vtkd.h sys/kbio.h])
    if test "$ac_cv_header_sys_kd_h" = 'no' \
	&& test "$ac_cv_header_sys_vtkd_h" = 'no' \
	&& test "$ac_cv_header_sys_kbio_h" = 'no' ; then

	WITH_CONSOLE='no'
	AC_MSG_WARN([Cannot find either sys/kd.h, sys/vtkd.h, or sys/kbio.h - support for console buzzer audio output will be disabled])
    else
	WITH_CONSOLE='yes'
    fi
fi

if test "$WITH_CONSOLE" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_CONSOLE], [1], [Define as 1 if your build machine can support console buzzer.])
fi



if test "$enable_oss" = "no" ; then
    WITH_OSS='no'
else
    AC_CHECK_HEADERS([soundcard.h sys/soundcard.h])
    if test "$ac_cv_header_soundcard_h" = 'no' \
	&& test "$ac_cv_header_sys_soundcard_h" = 'no' ; then

	WITH_OSS='no'
        AC_MSG_WARN([Cannot find either soundcard.h or sys/soundcard.h - support for OSS audio output will be disabled])
    else
	WITH_OSS='yes'
    fi
fi

if test "$WITH_OSS" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_OSS], [1], [Define as 1 if your build machine can support OSS.])
fi



if test "$enable_alsa" = "no" ; then
    WITH_ALSA='no'
else
    AC_CHECK_LIB(asound, snd_pcm_open)
    if test "$ac_cv_lib_asound_snd_pcm_open" = 'yes' ; then

	# Check for certain routines which are missing from liboss-salsa-dev
        # (kfreebsd's libasound implementation). If a function is found
	# build system defines HAVE_function_name in config.h.
	AC_CHECK_FUNCS([ \
	    snd_pcm_hw_params_test_periods \
	    snd_pcm_hw_params_test_buffer_size \
	    ])

	WITH_ALSA='yes'
    else
	WITH_ALSA='no'
	AC_MSG_WARN([Cannot find ALSA library files - support for ALSA audio output will be disabled])
    fi
fi

if test "$WITH_ALSA" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_ALSA], [1], [Define as 1 if your build machine can support ALSA.])
fi



if test "$enable_pulseaudio" = "no" ; then
    WITH_PULSEAUDIO='no'
else
    AC_CHECK_LIB(pulse-simple, pa_simple_new)
    if test "$ac_cv_lib_pulse_simple_pa_simple_new" = 'yes' ; then

	WITH_PULSEAUDIO='yes'
    else
	WITH_PULSEAUDIO='no'
	AC_MSG_WARN([Cannot find PulseAudio library files - support for PulseAudio audio output will be disabled])
    fi
fi

if test "$WITH_PULSEAUDIO" = 'yes' ; then
    AC_DEFINE([LIBCW_WITH_PULSEAUDIO], [1], [Define as 1 if your build machine can support PulseAudio.])
fi



if test "$enable_cwcp" = "no" ; then
   WITH_CWCP='no'
else
    AC_CHECK_LIB(curses, initscr)
    if test $ac_cv_lib_curses_initscr = 'yes' ; then
	WITH_CWCP='yes'
    else
	WITH_CWCP='no'
	AC_MSG_WARN([Cannot find libcurses - unable to build cwcp])
    fi
fi



if test "$enable_xcwcp" = "no" ; then
    WITH_XCWCP='no'
else
    # http://stackoverflow.com/questions/5178511/integrate-qt-project-with-autotool
    PKG_CHECK_MODULES(QT4, [QtCore QtGui >= 4.4.0], [
			   AC_PATH_PROGS(MOC, [moc-qt4 moc], moc,`eval $PKG_CONFIG --variable=exec_prefix QtCore`/bin)

			   AC_MSG_RESULT([QT4 CLFAGS is $QT4_CFLAGS])
			   AC_MSG_RESULT([QT4 LIBS is $QT4_LIBS])

			   AC_SUBST(QT4_CFLAGS)
			   AC_SUBST(QT4_LIBS)
			   AC_SUBST(MOC)

			   WITH_XCWCP='yes'

			   ], [

			   AC_MSG_WARN([Cannot find Qt4 files - unable to build xcwcp])

			   WITH_XCWCP='no'

			   ])
fi





# Development support tools.
AM_CONDITIONAL(LIBCW_WITH_DEV, test "$enable_dev" = "yes")
if test "$enable_dev" = "yes" ; then
   WITH_DEV='yes'
   AC_DEFINE([LIBCW_WITH_DEV], [1], [Define as 1 if you want to enable development support.])
else
   WITH_DEV='no'
fi


if test "$WITH_DEV" = 'yes' ; then
    LIBCW_NDEBUG=""
else
    LIBCW_NDEBUG="-DNDEBUG"
fi
AC_SUBST(LIBCW_NDEBUG)





# #####
#  end
# #####



# unixcw uses per-target linker object lists (target_LDADD)
LIBS=



# Checks for header files
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_HEADER_TIME
AC_CHECK_HEADERS([fcntl.h limits.h stdlib.h string.h strings.h sys/ioctl.h \
                  sys/param.h sys/time.h unistd.h locale.h libintl.h])
AC_CHECK_HEADERS([getopt.h])
AC_CHECK_HEADERS([string.h strings.h])
if test "$ac_cv_header_string_h" = 'no' \
    && test "$ac_cv_header_strings_h" = 'no' ; then
  AC_MSG_WARN([Cannot find either string.h or strings.h])
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T
AC_C_VOLATILE
AC_PROG_GCC_TRADITIONAL

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_STRCOLL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([floor gettimeofday memset sqrt strchr strdup strrchr \
                strtoul getopt_long setlocale memmove select strerror strspn])
AC_FUNC_SELECT_ARGTYPES




# Decide on which subdirectories to build; substitute into SRC_SUBDIRS.
# Build cwcp if curses is available, and xcwcp if Qt is available.
SRC_SUBDIRS="libcw cwutils cw cwgen"

if test "$WITH_CWCP" = 'yes' ; then
    SRC_SUBDIRS="$SRC_SUBDIRS cwcp"
fi

if test "$WITH_XCWCP" = 'yes' ; then
    SRC_SUBDIRS="$SRC_SUBDIRS xcwcp"
fi

AC_SUBST(SRC_SUBDIRS)



# Used for debug purposes.
# CFLAGS=`echo "$CFLAGS" | sed 's/-O2/-O0/g'`



# Add -Wall, -W, -pedantic, and other paranoia to gcc command flags, and -Wall
# to with a few suppressions to icc command flags, then similarly, though less
# so, for g++ and icpc.
if test "`basename $CC`" = "gcc" ; then
  CFLAGS="$CFLAGS -Wall -W -pedantic -Wfloat-equal -Wpointer-arith -std=gnu99"
  CFLAGS="$CFLAGS -Wcast-qual -Wwrite-strings -Wmissing-prototypes"
  CFLAGS="$CFLAGS -Wstrict-prototypes -Wmissing-declarations -Wredundant-decls"
else
  if test "`basename $CC`" = "icc" ; then
    CFLAGS="$CFLAGS -Wall -wd181,188,279,383,593,810,981,1469"
  fi
fi
if test "`basename $CXX`" = "g++" ; then
  # -Wno-variadic-macros is to silence g++'s warnings about C99
  # variadic macros from libcw_debug.h
  CXXFLAGS="$CXXFLAGS -Wall -W -pedantic -Wno-long-long -Wno-variadic-macros"
else
  if test "`basename $CXX`" = "icpc" ; then
    CXXFLAGS="$CXXFLAGS -Wall -wd181,188,279,383,593,810,981,1469"
  fi
fi

# Determine if -fPIC or -KPIC is available for building .so libraries.
# Because gcc complains about invalid flags, but then continues, we have to
# check by searching the compile stdout and stderr for any output.
if test -z "$CFLAG_PIC" ; then
  AC_MSG_CHECKING([for -KPIC or -fPIC compiler options])
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  if $CC -fPIC -c conftest.c 2>&1 | egrep -q '.' ; then
    if $CC -KPIC -c conftest.c 2>&1 | egrep -q '.' ; then
      CFLAG_PIC=""
    else
      CFLAG_PIC="-KPIC"
    fi
  else
    CFLAG_PIC="-fPIC"
  fi
  rm -f conftest.c conftest.o
  if test -n "$CFLAG_PIC" ; then
    AC_MSG_RESULT($CFLAG_PIC)
  else
    AC_MSG_RESULT(no)
  fi
fi
AC_SUBST(CFLAG_PIC)

# Determine if the C compiler builds DSO files, by testing with -shared.
CC_LINKS_SO="no"
if test -n "$CC" ; then
  AC_MSG_CHECKING(whether cc builds .so files with -shared)
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  $CC -c conftest.c >/dev/null 2>/dev/null
  $CC -shared -o conftest.so conftest.o >/dev/null 2>/dev/null
  rm -f conftest.c conftest.o
  if test -f conftest.so ; then
    nm conftest.so | grep -q so_test
    if test $? -eq 0 ; then
      CC_LINKS_SO="yes"
    fi
  fi
  rm -f conftest.so
  if test $CC_LINKS_SO = "yes" ; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi
fi
AC_SUBST(CC_LINKS_SO)

# A linker might also build DSO files with -G, but we'll prefer to use the
# compiler if it will cooperate.
LD_LINKS_SO="no"
AC_PATH_PROG(LD, ld, , )
if test -n "$LD" ; then
  AC_MSG_CHECKING(whether ld builds .so files with -G)
  cat >conftest.c <<-EOF
  int so_test() { return 0; }
EOF
  $CC -c conftest.c >/dev/null 2>/dev/null
  $LD -G -o conftest.so conftest.o >/dev/null 2>/dev/null
  rm -f conftest.c conftest.o
  if test -f conftest.so ; then
    nm conftest.so | grep -q so_test
    if test $? -eq 0 ; then
      LD_LINKS_SO="yes"
    fi
  fi
  rm -f conftest.so
  if test $LD_LINKS_SO = "yes" ; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
  fi
else
  LD=":"
fi
AC_SUBST(LD)
AC_SUBST(LD_LINKS_SO)





AC_CONFIG_FILES([Makefile.inc
	Makefile
	src/Makefile
	src/libcw/Makefile
	src/cwutils/Makefile
	src/cw/Makefile
	src/cwgen/Makefile])

if test "$WITH_CWCP" = 'yes' ; then
   AC_CONFIG_FILES([src/cwcp/Makefile])
fi
if test "$WITH_XCWCP" = 'yes' ; then
   AC_CONFIG_FILES([src/xcwcp/Makefile])
fi





AC_OUTPUT





# Put this at the very end, so that these are the last lines seen by
# user on console with low number of rows.
AC_MSG_NOTICE([----------------------------------------------])
AC_MSG_NOTICE([------ $PACKAGE_NAME $PACKAGE_VERSION build configuration ------])
AC_MSG_NOTICE([operating system:  ......................  $host_os])
AC_MSG_NOTICE([build libcw:  ...........................  yes])
AC_MSG_NOTICE([    libcw soname version:  ..............  $LIBCW_VERSION])
AC_MSG_NOTICE([    include console buzzer support:  ....  $WITH_CONSOLE])
AC_MSG_NOTICE([    include OSS support:  ...............  $WITH_OSS])
AC_MSG_NOTICE([    include ALSA support:  ..............  $WITH_ALSA])
AC_MSG_NOTICE([    include PulseAudio support:  ........  $WITH_PULSEAUDIO])
AC_MSG_NOTICE([build cw:  ..............................  yes])
AC_MSG_NOTICE([build cwgen:  ...........................  yes])
AC_MSG_NOTICE([build cwcp:  ............................  $WITH_CWCP])
AC_MSG_NOTICE([build xcwcp:  ...........................  $WITH_XCWCP])
if test "$WITH_XCWCP" = 'yes' ; then
    AC_MSG_NOTICE([    Qt4 CFLAGS:  $QT4_CFLAGS])
    AC_MSG_NOTICE([    Qt4 LIBS:    $QT4_LIBS])
    AC_MSG_NOTICE([    Qt4 MOC:     $MOC])
fi
if test "$WITH_DEV" = 'yes' ; then
   AC_MSG_NOTICE([enable dev support: .....................  yes])
fi
AC_MSG_NOTICE([----------------------------------------------])
