### assemble a list of programs we want to build and install

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
GST_LOADSAVE_SRC_V = 
else
GST_LOADSAVE_SRC = gst-xmllaunch
GST_LOADSAVE_SRC_V = \
	gst-xmllaunch-@GST_MAJORMINOR@
endif

if GST_DISABLE_PARSE
GST_PARSE_SRC = 
GST_PARSE_SRC_V = 
else
GST_PARSE_SRC = gst-launch
GST_PARSE_SRC_V = gst-launch-@GST_MAJORMINOR@
endif


GST_OTHER_SRC = \
	gst-feedback \
	gst-inspect \
	gst-typefind \
	gst-xmlinspect
GST_OTHER_SRC_V = \
	gst-inspect-@GST_MAJORMINOR@ \
	gst-typefind-@GST_MAJORMINOR@ \
	gst-xmlinspect-@GST_MAJORMINOR@

### so all of the programs we want to build
bin_PROGRAMS = \
	$(GST_LOADSAVE_SRC) $(GST_LOADSAVE_SRC_V) \
	$(GST_PARSE_SRC) $(GST_PARSE_SRC_V) \
	$(GST_OTHER_SRC) $(GST_OTHER_SRC_V)
bin_SCRIPTS = gst-feedback-@GST_MAJORMINOR@

# make sure each versioned tool has the right source file and flags
if !GST_DISABLE_LOADSAVE
gst_xmllaunch_@GST_MAJORMINOR@_SOURCES = gst-launch.c tools.h
gst_xmllaunch_@GST_MAJORMINOR@_CFLAGS = $(GST_OBJ_CFLAGS) -UGST_DISABLE_DEPRECATED
gst_xmllaunch_@GST_MAJORMINOR@_LDADD = $(GST_OBJ_LIBS)
endif

if !GST_DISABLE_PARSE
gst_launch_@GST_MAJORMINOR@_SOURCES = gst-launch.c tools.h
gst_launch_@GST_MAJORMINOR@_CFLAGS = $(GST_OBJ_CFLAGS) -UGST_DISABLE_DEPRECATED
gst_launch_@GST_MAJORMINOR@_LDADD = $(GST_OBJ_LIBS)
endif

gst_inspect_@GST_MAJORMINOR@_SOURCES = gst-inspect.c tools.h
gst_inspect_@GST_MAJORMINOR@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_inspect_@GST_MAJORMINOR@_LDADD = $(GST_OBJ_LIBS)
gst_typefind_@GST_MAJORMINOR@_SOURCES = gst-typefind.c tools.h
gst_typefind_@GST_MAJORMINOR@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_typefind_@GST_MAJORMINOR@_LDADD = $(GST_OBJ_LIBS)
gst_xmlinspect_@GST_MAJORMINOR@_SOURCES = gst-xmlinspect.c tools.h
gst_xmlinspect_@GST_MAJORMINOR@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_xmlinspect_@GST_MAJORMINOR@_LDADD = $(GST_OBJ_LIBS)

gst-feedback-@GST_MAJORMINOR@: gst-feedback-m.m
	$(AM_V_GEN)cp $(srcdir)/gst-feedback-m.m $@ && \
	chmod +x $@

# make sure each unversioned tool comes from gst-run.c
if !GST_DISABLE_LOADSAVE
gst_xmllaunch_SOURCES = gst-run.c
endif

if !GST_DISABLE_PARSE
gst_launch_SOURCES = gst-run.c
endif

gst_feedback_SOURCES = gst-run.c
gst_inspect_SOURCES = gst-run.c
gst_typefind_SOURCES = gst-run.c
gst_xmlinspect_SOURCES = gst-run.c

# CFLAGS and libs for nonversioned frontend binaries
AM_CFLAGS = $(GLIB_ONLY_CFLAGS)
LDADD = $(GLIB_ONLY_LIBS)
# due to depcomp not using AM_CFLAGS for rh9/yd3, we also set AM_CPPFLAGS
AM_CPPFLAGS = $(GLIB_ONLY_CFLAGS)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:EXECUTABLE gst-inspect -:TAGS eng debug \
	 -:SOURCES $(gst_inspect_@GST_MAJORMINOR@_SOURCES) \
	 -:CFLAGS $(DEFS) $(gst_inspect_@GST_MAJORMINOR@_CFLAGS) \
	 -:LDFLAGS $(gst_inspect_@GST_MAJORMINOR@_LDADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	 -:EXECUTABLE gst-launch -:TAGS eng debug \
	 -:SOURCES $(gst_launch_@GST_MAJORMINOR@_SOURCES) \
	 -:CFLAGS $(DEFS) $(gst_launch_@GST_MAJORMINOR@_CFLAGS) \
	 -:LDFLAGS $(gst_launch_@GST_MAJORMINOR@_LDADD) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

### man pages we want to install
if GST_DISABLE_LOADSAVE
GST_LOADSAVE_MAN=
else
GST_LOADSAVE_MAN = \
	gst-xmllaunch-@GST_MAJORMINOR@.1
endif

if GST_DISABLE_PARSE
GST_PARSE_MAN=
else
GST_PARSE_MAN = \
	gst-launch-@GST_MAJORMINOR@.1
endif


GST_OTHER_MAN = \
	gst-feedback-@GST_MAJORMINOR@.1 \
	gst-inspect-@GST_MAJORMINOR@.1 \
	gst-typefind-@GST_MAJORMINOR@.1 \
	gst-xmlinspect-@GST_MAJORMINOR@.1

manpages = $(GST_LOADSAVE_MAN) $(GST_PARSE_MAN) $(GST_OTHER_MAN)

CLEANFILES = $(manpages) $(bin_SCRIPTS) *.gcno *.gcda
man_MANS = $(manpages)

# developer helper tools, not meant for installation
noinst_SCRIPTS = gst-indent

noinst_HEADERS = tools.h

EXTRA_DIST = \
	$(noinst_SCRIPTS) \
	gst-feedback.1.in \
	gst-inspect.1.in \
	gst-launch.1.in \
	gst-typefind.1.in \
	gst-xmlinspect.1.in \
	gst-xmllaunch.1.in \
	gst-feedback-m.m \
	gst-plot-timeline.py

%-@GST_MAJORMINOR@.1: %.1.in
	$(AM_V_GEN)sed \
		-e s,gst-feedback,gst-feedback-@GST_MAJORMINOR@,g \
		-e s,gst-inspect,gst-inspect-@GST_MAJORMINOR@,g \
		-e s,gst-launch,gst-launch-@GST_MAJORMINOR@,g \
		-e s,gst-typefind,gst-typefind-@GST_MAJORMINOR@,g \
		-e s,gst-xmlinspect,gst-xmlinspect-@GST_MAJORMINOR@,g \
		-e s,gst-xmllaunch,gst-xmllaunch-@GST_MAJORMINOR@,g \
		-e s,GST_MAJORMINOR,@GST_MAJORMINOR@,g \
		$< >$@

