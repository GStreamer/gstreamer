DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

# note: keep in sync with configure.ac
ACLOCAL_AMFLAGS = -I m4 -I common/m4

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-1.0.m4

SUBDIRS = pkgconfig \
	gst libs plugins tests \
	docs \
	po \
	m4 \
	common \
	data

if BUILD_TOOLS
SUBDIRS += tools
endif

# These are all the possible subdirs
DIST_SUBDIRS = pkgconfig \
	gst libs plugins tools tests \
	docs \
	po \
	m4 \
	common \
	data

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-element-check-1.0.m4 \
	configure.ac autogen.sh depcomp \
	MAINTAINERS ABOUT-NLS RELEASE gstreamer.doap \
	scripts/gst-uninstalled \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/gstreamer-*" ) \
	gst/gstenumtypes.c.template gst/gstenumtypes.h.template \
	libs/gst/controller/controller_mkenum.py \
	meson_options.txt \
	gst/parse/gen_lex.py.in gst/parse/gen_grammar.py.in \
	$(win32)

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
	$(MAKE) -C tests/check check-valgrind

check-torture:
	$(MAKE) -C tests/check torture

build-checks:
	$(MAKE) -C tests/check build-checks
else
check-valgrind:
	echo "'check' library not installed, skipping"

check-torture:
	echo "'check' library not installed, skipping"

build-checks:
	echo "'check' library not installed, skipping"
endif

check-enum-gettypes: $(top_builddir)/gst/gstenumtypes.h
	@echo 'Checking if all enum and flag _get_type() funcs are inited in gst_init()';                        \
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
if !GST_DISABLE_REGISTRY
CHECK_EXPORTS = check-exports
endif

check: $(CHECK_EXPORTS) check-enum-gettypes

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/win32/common/config.h-new \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/codeset.m4 \
	$(top_builddir)/common/m4/gettext.m4 \
	$(top_builddir)/common/m4/glibc2.m4 \
	$(top_builddir)/common/m4/glibc21.m4 \
	$(top_builddir)/common/m4/iconv.m4 \
	$(top_builddir)/common/m4/intdiv0.m4 \
	$(top_builddir)/common/m4/intl.m4 \
	$(top_builddir)/common/m4/intldir.m4 \
	$(top_builddir)/common/m4/intlmacosx.m4 \
	$(top_builddir)/common/m4/intmax.m4 \
	$(top_builddir)/common/m4/inttypes-pri.m4 \
	$(top_builddir)/common/m4/inttypes_h.m4 \
	$(top_builddir)/common/m4/lcmessage.m4 \
	$(top_builddir)/common/m4/lib-ld.m4 \
	$(top_builddir)/common/m4/lib-link.m4 \
	$(top_builddir)/common/m4/lib-prefix.m4 \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/lock.m4 \
	$(top_builddir)/common/m4/longlong.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4 \
	$(top_builddir)/common/m4/nls.m4 \
	$(top_builddir)/common/m4/po.m4 \
	$(top_builddir)/common/m4/printf-posix.m4 \
	$(top_builddir)/common/m4/progtest.m4 \
	$(top_builddir)/common/m4/size_max.m4 \
	$(top_builddir)/common/m4/stdint_h.m4 \
	$(top_builddir)/common/m4/uintmax_t.m4 \
	$(top_builddir)/common/m4/visibility.m4 \
	$(top_builddir)/common/m4/wchar_t.m4 \
	$(top_builddir)/common/m4/wint_t.m4 \
	$(top_builddir)/common/m4/xsize.m4

CRUFT_DIRS = \
	$(top_builddir)/tests/examples/xml \
	$(top_builddir)/tests/examples/launch \
	$(top_builddir)/tests/examples/metadata \
	$(top_builddir)/tests/examples/queue \
	$(top_builddir)/tests/examples/typefind \
	$(top_builddir)/gst/tmp-introspect* \
	$(top_builddir)/libs/gst/*/tmp-introspect* \
	$(top_srcdir)/docs/plugins/tmpl

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
