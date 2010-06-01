EXAMPLES =

if USE_X

if HAVE_GTK_X11
EXAMPLES += gtk-xoverlay

gtk_xoverlay_SOURCES = gtk-xoverlay.c
gtk_xoverlay_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS) $(GTK_CFLAGS)
gtk_xoverlay_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) $(GTK_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la
endif

if HAVE_QT
EXAMPLES += qt-xoverlay

qt_xoverlay_SOURCES = qt-xoverlay.cpp
qt_xoverlay_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(X_CFLAGS) $(QT_CFLAGS)
qt_xoverlay_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) $(QT_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la

endif

if HAVE_QT_GV
EXAMPLES += qtgv-xoverlay

qtgv_xoverlay_SOURCES = qtgv-xoverlay.cpp qtgv-xoverlay.h
qtgv_xoverlay_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(X_CFLAGS) $(QT_CFLAGS)
qtgv_xoverlay_LDADD = $(GST_LIBS) $(X_LIBS) $(LIBM) $(QT_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la

# qt moc support, according to http://qtnode.net/wiki/Qt_with_autotools

nodist_qtgv_xoverlay_SOURCES = moc_qtgv-xoverlay.cpp

moc_%.cpp:%.h
	$(AM_V_GEN)$(QT4_MOC) $< -o $@

EXTRA_DIST = $(nodist_qtgv_xoverlay_SOURCES:moc_%.cpp=%.h)
CLEANFILES = $(nodist_qtgv_xoverlay_SOURCES)

endif

endif

noinst_PROGRAMS = $(EXAMPLES)

