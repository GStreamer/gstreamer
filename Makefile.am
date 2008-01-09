DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc --enable-docbook

all-local: gst-element-check-@GST_MAJORMINOR@.m4

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

ACLOCAL_AMFLAGS = -I common/m4

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS = \
	gst libs plugins tools tests \
	docs \
	pkgconfig po \
	common

# These are all the possible subdirs
DIST_SUBDIRS = \
	gst libs plugins tools tests \
	docs \
	pkgconfig po \
	common

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gstreamer.spec gstreamer.spec.in \
	gst-element-check.m4.in \
	configure.ac autogen.sh depcomp \
	MAINTAINERS ABOUT-NLS RELEASE gstreamer.doap \
	$(win32)

CLEANFILES = gst-element-check-@GST_MAJORMINOR@.m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

if GST_GCOV_ENABLED
clean-gcov:
	find -name "*.da" -o -name "*.gcov" | xargs rm || true

clean-bbg:
	find -name "*.bbg" -o -name "*.bb" | xargs rm || true

GCOV_DIRS=gst libs

## .PHONY so it always rebuilds it
.PHONY: coverage-report.txt

coverage-report.txt:
	BBG_FILES=`find $(GCOV_DIRS) -name "*.bbg"` ;			  \
	C_FILES= ;							  \
	for F in $$BBG_FILES ; do					  \
		F_nolibs=`echo $$F | sed -e 's/.libs\///g'` ;		  \
		C=`echo $$F_nolibs | sed -e 's/.bbg/.c/g'` ;		  \
		B=`basename $$F .bbg` ;					  \
		D=`dirname $$F` ;					  \
		DA=`echo $$F | sed -e 's/.bbg/.da/g'` ;			  \
		DA_libs=`echo $$D/.libs/$$B/.da` ;			  \
		if test -e $$DA || test -e $$DA_libs; then		  \
			C_FILES="$$C_FILES $$C" ;			  \
		fi ;							  \
	done ;								  \
	echo $$C_FILES ;						  \
	$(top_builddir)/testsuite/decode-gcov --report $$C_FILES > coverage-report.txt

check-coverage: clean-gcov all check coverage-report.txt
	cat coverage-report.txt

else
coverage-report.txt:
	echo "Need to reconfigure with --enable-gcov"

check-coverage:
	echo "Need to reconfigure with --enable-gcov"
endif

if HAVE_CHECK
check-valgrind:
	cd tests/check && make check-valgrind

check-torture:
	cd tests/check && make torture
else
check-valgrind:
	echo "'check' library not installed, skipping"

check-torture:
	echo "'check' library not installed, skipping"
endif

# FIXME: this target should be run every time we do "make release"
# find a way of automating that
win32-update:
	for f in gstversion.h gstenumtypes.c gstenumtypes.h; do \
		cp $(top_builddir)/gst/$$f win32/common; done
	$(top_srcdir)/tools/gst-indent win32/common/gstenumtypes.c

include $(top_srcdir)/common/coverage/lcov.mak

check: check-exports

