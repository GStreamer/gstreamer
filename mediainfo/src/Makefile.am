bin_PROGRAMS = gst-mi

INCLUDES = \
	-include config.h \
	$(MI_CFLAGS) \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPKGLIBDIR=\""$(pkglibdir)"\"

VALAFLAGS = \
	--vapidir=$(top_srcdir)/vapi --pkg config \
	@MI_PACKAGES@

gst_mi_SOURCES = \
	mi.vala \
	mi-app.vala \
	mi-info.vala

gst_mi_LDADD = \
	$(MI_LIBS) -lgstinterfaces-0.10 -lgstpbutils-0.10

pixmapsdir = $(pkgdatadir)/ui/icons
pixmaps_DATA = gst-mi.png

iconsdir = $(datadir)/pixmaps/
icons_DATA = gst-mi.png

desktopdir = $(datadir)/applications/
desktop_DATA = gst-mi.desktop
@INTLTOOL_DESKTOP_RULE@

install-data-hook:
	test -z "$(UPDATE_DESKTOP_DATABASE)" || $(UPDATE_DESKTOP_DATABASE) "$(DESTDIR)$(desktopdir)";
uninstall-local:
	test -z "$(UPDATE_DESKTOP_DATABASE)" || $(UPDATE_DESKTOP_DATABASE) "$(DESTDIR)$(desktopdir)";

EXTRA_DIST = gst-mi.png gst-mi.desktop.in

BUILT_SOURCES = $(gst_mi_SOURCES:.vala=.c)

CLEANFILES = \
  $(BUILT_SOURCES) gst_mi_vala.stamp gst-mi.desktop

-include $(top_srcdir)/git.mk

