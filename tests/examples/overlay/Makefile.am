EXAMPLES =

if USE_X

if HAVE_GTK_X11
EXAMPLES += gtk-videooverlay

gtk_videooverlay_SOURCES = gtk-videooverlay.c
gtk_videooverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS) $(GTK_CFLAGS)
gtk_videooverlay_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_LIBS) $(X_LIBS) $(LIBM) $(GTK_LIBS)
endif

if HAVE_QT
EXAMPLES += qt-videooverlay

qt_videooverlay_SOURCES = qt-videooverlay.cpp
qt_videooverlay_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(X_CFLAGS) $(QT_CFLAGS) -fPIC
qt_videooverlay_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_LIBS) $(X_LIBS) $(LIBM) $(QT_LIBS)
endif

if HAVE_QT_MOC
EXAMPLES += qtgv-videooverlay

qtgv_videooverlay_SOURCES = qtgv-videooverlay.cpp qtgv-videooverlay.h
qtgv_videooverlay_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(X_CFLAGS) $(QT_CFLAGS) -fPIC
qtgv_videooverlay_LDADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_LIBS) $(X_LIBS) $(LIBM) $(QT_LIBS)

# qt moc support, according to http://qtnode.net/wiki/Qt_with_autotools

nodist_qtgv_videooverlay_SOURCES = moc_qtgv-videooverlay.cpp

moc_%.cpp:%.h
	$(AM_V_GEN)$(QT_MOC) $< -o $@

EXTRA_DIST = $(nodist_qtgv_videooverlay_SOURCES:moc_%.cpp=%.h)
CLEANFILES = $(nodist_qtgv_videooverlay_SOURCES)
endif

endif

noinst_PROGRAMS = $(EXAMPLES)

