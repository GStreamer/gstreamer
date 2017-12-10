noinst_PROGRAMS = ##

#works on win32 and X
if HAVE_CLUTTER

noinst_PROGRAMS += cluttershare

cluttershare_SOURCES = cluttershare.c

cluttershare_CFLAGS= \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GL_CFLAGS) $(CLUTTER_CFLAGS)
cluttershare_LDADD= \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) $(CLUTTER_LIBS)
endif


if HAVE_CLUTTER_GLX
if HAVE_CLUTTER_X11
if HAVE_XCOMPOSITE

noinst_PROGRAMS += clutteractor clutteractortee

clutteractor_SOURCES = clutteractor.c

clutteractor_CFLAGS= \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(CLUTTER_CFLAGS) \
	$(CLUTTER_GLX_CFLAGS) $(CLUTTER_X11_CFLAGS) $(XCOMPOSITE_CFLAGS)
clutteractor_LDADD= \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) \
	$(CLUTTER_LIBS) $(CLUTTER_GLX_LIBS) $(CLUTTER_X11_LIBS) \
	$(XCOMPOSITE_LIBS)

clutteractortee_SOURCES = clutteractortee.c

clutteractortee_CFLAGS= \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GL_CFLAGS) $(CLUTTER_CFLAGS) \
	$(CLUTTER_GLX_CFLAGS) $(CLUTTER_X11_CFLAGS) $(XCOMPOSITE_CFLAGS)
clutteractortee_LDADD= \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_LIBS) $(GL_LIBS) \
	$(CLUTTER_LIBS) $(CLUTTER_GLX_LIBS) $(CLUTTER_X11_LIBS) \
	$(XCOMPOSITE_LIBS)

endif
endif
endif
