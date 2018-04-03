SUBDIRS = common gi plugin testsuite

# Examples and testsuite need to be ported to 1.0
#examples testsuite

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

EXTRA_DIST = \
	configure.ac autogen.sh depcomp \
	RELEASE gst-python.doap \
	meson.build \
	meson_options.txt \
	config.h.meson \
	pygst.py.in \
	scripts/pythondetector \
	examples/plugins/python/sinkelement.py \
	examples/plugins/python/identity.py \
	examples/helloworld.py
