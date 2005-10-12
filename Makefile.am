DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if USE_GCONFTOOL
GCONF_DIR = gconf
else
GCONF_DIR =
endif

if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif

ALWAYS_SUBDIRS =		\
	gst sys ext 		\
	docs			\
	po			\
	m4 common

SUBDIRS = 			\
	$(ALWAYS_SUBDIRS)	\
	$(SUBDIRS_CHECK)	\
	$(GCONF_DIR)

DIST_SUBDIRS = 			\
	$(ALWAYS_SUBDIRS)	\
	check			\
	gconf

EXTRA_DIST = \
	gst-plugins-good.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

noinst_HEADERS = \
	gst-libs/gst/gettext.h \
	gst-libs/gst/gst-i18n-plugin.h

include $(top_srcdir)/common/release.mak
# include $(top_srcdir)/common/po.mak

check-valgrind:
	cd check && make check-valgrind
