if USE_GCONFTOOL
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if BUILD_EXTERNAL
EXT_DIR=ext
else
EXT_DIR=
endif

if BUILD_EXAMPLES
EXAMPLES_DIR=examples
else
EXAMPLES_DIR=
endif

SUBDIRS=gst-libs \
	gst sys $(EXT_DIR) \
	$(EXAMPLES_DIR) \
	tools \
	$(GCONF_DIR) \
	testsuite \
	po \
	common \
	m4 \
	pkgconfig

DIST_SUBDIRS=ext gst-libs \
	gst sys \
	examples \
	tools \
	gconf \
	testsuite \
	po \
	common \
	m4 \
	pkgconfig

EXTRA_DIST=gst-plugins.spec depcomp \
	   AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh

DISTCLEANFILES=_stdint.h

include $(top_srcdir)/common/release.mak
# a rule to redownload po files
download-po:
	cd po && for LI in `cat LINGUAS`; do echo Checking $$LI.po; wget -q -O $$LI.po.tmp http://www.iro.umontreal.ca/translation/maint/gst-plugins/$$LI.po; if ! diff $$LI.po $$LI.po.tmp > /dev/null 2>&1; then echo "Updating $$LI.po"; mv $$LI.po.tmp $$LI.po; else rm $$LI.po.tmp; fi; done
