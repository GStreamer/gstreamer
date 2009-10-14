DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc --enable-docbook

all-local: gst-element-check-@GST_MAJORMINOR@.m4

gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

# note: keep in sync with configure.ac
ACLOCAL_AMFLAGS = -I common/m4 -I .

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS = pkgconfig \
	gst libs plugins tools tests \
	docs \
	po \
	common

# These are all the possible subdirs
DIST_SUBDIRS = pkgconfig \
	gst libs plugins tools tests \
	docs \
	po \
	common

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gstreamer.spec gstreamer.spec.in \
	check-checks.m4 \
	gst-element-check.m4.in \
	configure.ac autogen.sh depcomp \
	MAINTAINERS ABOUT-NLS RELEASE gstreamer.doap \
	$(win32)

CLEANFILES = gst-element-check-@GST_MAJORMINOR@.m4

DISTCLEANFILES = _stdint.h

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
	cp $(top_builddir)/win32/common/config.h-new \
		$(top_srcdir)/win32/common/config.h

check-enum-gettypes: $(top_builddir)/gst/gstenumtypes.h
	echo 'Checking if all enum and flag _get_type() funcs are inited in gst_init()';                         \
	FUNCS=`grep '_get_type (' gst/gstenumtypes.h | sed -e 's/^.*gst_/gst_/' -e 's/_get_type.*$$/_get_type/'`;\
	MISSING_FUNCS='';                                                                                        \
	for f in $$FUNCS; do                                                                                     \
	  INIT_LINE=`grep $$f $(top_srcdir)/gst/*.c | grep g_type_class_ref`;                                    \
	  if test "x$$INIT_LINE" = "x"; then                                                                     \
	    MISSING_FUNCS="$$MISSING_FUNCS $$f";                                                                 \
	  fi;                                                                                                    \
	done;                                                                                                    \
	if test "x$$MISSING_FUNCS" != "x"; then                                                                  \
	  echo "FIXME: please add to the following to gst/gst.c init_post():";                                   \
	  for f in $$MISSING_FUNCS; do                                                                           \
	    echo "  g_type_class_ref ($$f ());";                                                                 \
	  done;                                                                                                  \
	  exit 1;                                                                                                \
	fi;                                                                                                      \
	echo 'Checking if all enum and flag _get_type() funcs are deinited in gst_deinit()';                     \
	MISSING_FUNCS='';                                                                                        \
	for f in $$FUNCS; do                                                                                     \
	  INIT_LINE=`grep $$f $(top_srcdir)/gst/*.c | grep g_type_class_unref`;                                  \
	  if test "x$$INIT_LINE" = "x"; then                                                                     \
	    MISSING_FUNCS="$$MISSING_FUNCS $$f";                                                                 \
	  fi;                                                                                                    \
	done;                                                                                                    \
	if test "x$$MISSING_FUNCS" != "x"; then                                                                  \
	  echo "FIXME: please add to the following to gst/gst.c gst_deinit():";                                  \
	  for f in $$MISSING_FUNCS; do                                                                           \
	    echo "  g_type_class_unref (g_type_class_peek ($$f ()));";                                           \
	  done;                                                                                                  \
	  exit 1;                                                                                                \
	fi

include $(top_srcdir)/common/coverage/lcov.mak

# Do not run the check-exports test in case any option which causes the API to
# change has been used
if !GST_DISABLE_GST_DEBUG
if !GST_DISABLE_LOADSAVE
if !GST_DISABLE_REGISTRY
if !GST_DISABLE_TRACE
CHECK_EXPORTS = check-exports
endif
endif
endif
endif

check: $(CHECK_EXPORTS) check-enum-gettypes
