SUBDIRS = common omx tools config m4 tests

if BUILD_EXAMPLES
SUBDIRS += examples
endif

ACLOCAL_AMFLAGS = -I m4 -I common/m4

DISTCLEANFILES = _stdint.h

EXTRA_DIST = autogen.sh gst-omx.doap RELEASE \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	meson_options.txt

-include $(top_srcdir)/common/release.mak
