PACKAGE = @PACKAGE@
VERSION = @VERSION@

if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

if BUILD_TESTS
SUBDIRS_TESTS = tests testsuite
else
SUBDIRS_TESTS =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

all-local: gst-element-check-@GST_MAJORMINOR@.m4

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp $(srcdir)/gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

ACLOCAL_AMFLAGS = -I common/m4

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS = include gst libs tools \
	$(SUBDIRS_TESTS) $(SUBDIRS_EXAMPLES) \
	pkgconfig po \
	$(SUBDIRS_DOCS)

# These are all the possible subdirs
DIST_SUBDIRS = include libs gst \
	tools \
	tests testsuite \
	examples \
	pkgconfig \
	po \
	docs

EXTRA_DIST = gstreamer.spec.in gst-element-check.m4 \
	     configure.ac autogen.sh depcomp \
	     REQUIREMENTS ABOUT-NLS RELEASE DOCBUILDING DEVEL \
	     idiottest.mak common

CLEANFILES = gst-element-check-@GST_MAJORMINOR@.m4


dist-hook:
	cp gstreamer.spec $(distdir)
	rm -rf `find $(distdir)/common -name CVS`

rpm: dist
	rpm -ta $(distdir).tar.gz

include $(top_srcdir)/idiottest.mak


if GST_GCOV_ENABLED
clean-gcov:
	find -name "*.da" -o -name "*.gcov" | xargs rm || true

clean-bbg:
	find -name "*.bbg" -o -name "*.bb" | xargs rm || true

GCOV_DIRS=gst libs

## .PHONY so it always rebuilds it
.PHONY: coverage-report.txt
coverage-report.txt:
	BBG_FILES=`find $(GCOV_DIRS) -name "*.bbg"` ;				  \
	C_FILES= ;								  \
	for F in $$BBG_FILES ; do						  \
		F_nolibs=`echo $$F | sed -e 's/.libs\///g'` ;			  \
		C=`echo $$F_nolibs | sed -e 's/.bbg/.c/g'` ;			  \
		B=`basename $$F .bbg` ;						  \
		D=`dirname $$F` ;						  \
		DA=`echo $$F | sed -e 's/.bbg/.da/g'` ;				  \
		DA_libs=`echo $$D/.libs/$$B/.da` ;				  \
		if test -e $$DA || test -e $$DA_libs; then			  \
			C_FILES="$$C_FILES $$C" ;				  \
		fi ;								  \
	done ;									  \
	echo $$C_FILES ;							  \
	$(top_builddir)/testsuite/decode-gcov --report $$C_FILES > coverage-report.txt

check-coverage: clean-gcov all check coverage-report.txt
	cat coverage-report.txt

else
coverage-report.txt:
	echo "Need to reconfigure with --enable-gcov"

check-coverage:
	echo "Need to reconfigure with --enable-gcov"

endif
