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

pixmapsdir=$(pkgdatadir)/ui/icons
pixmaps_DATA=gst-mi.png

EXTRA_DIST = gst-mi.png

BUILT_SOURCES = $(gst_mi_SOURCES:.vala=.c)

CLEANFILES = \
  $(BUILT_SOURCES) gst_mi_vala.stamp

-include $(top_srcdir)/git.mk

