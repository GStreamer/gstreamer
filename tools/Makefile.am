
bin_PROGRAMS = \
	gst-inspect-@GST_API_VERSION@ \
	gst-typefind-@GST_API_VERSION@

gst_inspect_@GST_API_VERSION@_SOURCES = gst-inspect.c tools.h
gst_inspect_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_inspect_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

gst_typefind_@GST_API_VERSION@_SOURCES = gst-typefind.c tools.h
gst_typefind_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_typefind_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

if !GST_DISABLE_PARSE
bin_PROGRAMS += gst-launch-@GST_API_VERSION@

gst_launch_@GST_API_VERSION@_SOURCES = gst-launch.c tools.h
gst_launch_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_launch_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)
endif

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:EXECUTABLE gst-inspect-@GST_API_VERSION@ -:TAGS eng debug \
	 -:SOURCES $(gst_inspect_@GST_API_VERSION@_SOURCES) \
	 -:CFLAGS $(DEFS) $(gst_inspect_@GST_API_VERSION@_CFLAGS) \
	 -:LDFLAGS $(gst_inspect_@GST_API_VERSION@_LDADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	 -:EXECUTABLE gst-launch-@GST_API_VERSION@ -:TAGS eng debug \
	 -:SOURCES $(gst_launch_@GST_API_VERSION@_SOURCES) \
	 -:CFLAGS $(DEFS) $(gst_launch_@GST_API_VERSION@_CFLAGS) \
	 -:LDFLAGS $(gst_launch_@GST_API_VERSION@_LDADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

manpages = \
	gst-inspect-@GST_API_VERSION@.1 \
	gst-typefind-@GST_API_VERSION@.1

if !GST_DISABLE_PARSE
manpages += gst-launch-@GST_API_VERSION@.1
endif

CLEANFILES = $(manpages) *.gcno *.gcda
man_MANS = $(manpages)

# developer helper tools, not meant for installation
noinst_SCRIPTS = gst-indent

noinst_HEADERS = tools.h

EXTRA_DIST = \
	$(noinst_SCRIPTS) \
	gst-inspect.1.in \
	gst-launch.1.in \
	gst-typefind.1.in

%-@GST_API_VERSION@.1: %.1.in
	$(AM_V_GEN)sed \
		-e s,gst-inspect,gst-inspect-@GST_API_VERSION@,g \
		-e s,gst-launch,gst-launch-@GST_API_VERSION@,g \
		-e s,gst-typefind,gst-typefind-@GST_API_VERSION@,g \
		-e s,GST_API_VERSION,@GST_API_VERSION@,g \
		$< >$@

