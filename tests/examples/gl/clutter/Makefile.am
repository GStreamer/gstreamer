noinst_PROGRAMS = ##

#works on win32 and X
if HAVE_CLUTTER

noinst_PROGRAMS += cluttershare

cluttershare_SOURCES = cluttershare.c

cluttershare_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
    $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_CFLAGS) $(GL_CFLAGS) $(CLUTTER_CFLAGS)
cluttershare_LDADD=$(CLUTTER_LIBS) $(GST_PLUGINS_BASE_LIBS) \
    $(GST_LIBS) $(GL_LIBS) \
    -lgstvideo-$(GST_API_VERSION) $(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la

endif


if HAVE_CLUTTER_GLX
if HAVE_CLUTTER_X11
if HAVE_XCOMPOSITE

noinst_PROGRAMS += clutteractor clutteractortee

clutteractor_SOURCES = clutteractor.c

clutteractor_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
    $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(CLUTTER_CFLAGS) \
	$(CLUTTER_GLX_CFLAGS) $(CLUTTER_X11_CFLAGS) $(XCOMPOSITE_CFLAGS)
clutteractor_LDADD=$(CLUTTER_LIBS) $(CLUTTER_GLX_LIBS) $(CLUTTER_X11_LIBS) \
	$(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) $(XCOMPOSITE_LIBS) -lgstvideo-$(GST_API_VERSION)

clutteractortee_SOURCES = clutteractortee.c

clutteractortee_CFLAGS=-I$(top_srcdir)/gst-libs -I$(top_builddir)/gst-libs \
    $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(CLUTTER_CFLAGS) \
	$(CLUTTER_GLX_CFLAGS) $(CLUTTER_X11_CFLAGS) $(XCOMPOSITE_CFLAGS)
clutteractortee_LDADD=$(CLUTTER_LIBS) $(CLUTTER_GLX_LIBS) $(CLUTTER_X11_LIBS) \
	$(GST_PLUGINS_GL_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS) $(XCOMPOSITE_LIBS) -lgstvideo-$(GST_API_VERSION)

endif
endif
endif
