DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc --enable-docbook

if BUILD_TESTS
## SUBDIRS_TESTS = tests testsuite
## FIXME: write tests from scratch
SUBDIRS_TESTS =
if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif
else
SUBDIRS_TESTS =
SUBDIRS_CHECK =
endif

if BUILD_EXAMPLES
## FIXME: write examples from scratch
# SUBDIRS_EXAMPLES = examples
SUBDIRS_EXAMPLES =
else
SUBDIRS_EXAMPLES =
endif

all-local: gst-element-check-@GST_MAJORMINOR@.m4

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp $(srcdir)/gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

ACLOCAL_AMFLAGS = -I common/m4

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS = \
	gst libs plugins tools \
	$(SUBDIRS_CHECK) \
	$(SUBDIRS_TESTS) \
	$(SUBDIRS_EXAMPLES) \
	docs \
	pkgconfig po \
	common

# These are all the possible subdirs
DIST_SUBDIRS = \
	gst libs plugins tools \
	check \
	tests testsuite \
	examples \
	docs \
	pkgconfig po \
	common

win32 = $(shell cat $(top_srcdir)/win32/MANIFEST)

debug:
	echo $(win32)
EXTRA_DIST = \
	gstreamer.spec gstreamer.spec.in gst-element-check.m4 \
	configure.ac autogen.sh depcomp \
	ABOUT-NLS RELEASE \
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
.PHONY: coverage-report.txt test-coverage-report.html

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

test-coverage-report.html:
	mkdir -p ./coverage
	lcov --directory . --zerocounters
	-$(MAKE) check
	lcov --directory . --capture --output-file ./coverage/$(PACKAGE).info
	genhtml -o ./coverage --num-spaces 2 ./coverage/$(PACKAGE).info
	
check-coverage: clean-gcov all check coverage-report.txt
	cat coverage-report.txt

else
coverage-report.txt:
	echo "Need to reconfigure with --enable-gcov"
	
test-coverage-report.html:
	echo "Need to reconfigure with --enable-gcov"

check-coverage:
	echo "Need to reconfigure with --enable-gcov"
endif

if HAVE_CHECK
check-valgrind:
	cd check && make check-valgrind

check-torture:
	cd check && make torture
else
check-valgrind:
	echo "You need to install the 'check' library"
	exit 1

check-torture:
	echo "You need to install the 'check' library"
	exit 1
endif

win32-update:
	for f in gstversion.h gstenumtypes.c gstenumtypes.h; do \
		cp $(top_builddir)/gst/$$f win32/common; done
