SUBDIRS = common gi plugin

# Examples and testsuite need to be ported to 1.0
#examples testsuite

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak

# Examples and testsuite need to be ported to 1.0
# check-valgrind:
# 	cd testsuite/ && make check-valgrind

check-torture:
	@true

#include $(top_srcdir)/common/coverage/lcov.mak

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
