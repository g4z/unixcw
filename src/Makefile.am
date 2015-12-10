# use $(top_builddir) instead of $(top_srcdir) because Makefile.inc is
# (re)created at build time; if you use $(top_srcdir), you may (will?)
# get empty, non-initialized $(AC_SRC_SUBDIRS);
-include $(top_builddir)/Makefile.inc

# may (or may not) include cwcp and xcwcp
SUBDIRS = $(AC_SRC_SUBDIRS)

