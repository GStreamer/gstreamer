### assemble a list of programs we want to build and install
if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
GST_REGISTRY_SRC_V = 
else
GST_REGISTRY_SRC = gst-register
GST_REGISTRY_SRC_V = gst-register-@GST_MAJORMINOR@
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
GST_LOADSAVE_SRC_V = 
else
GST_LOADSAVE_SRC = gst-complete gst-compprep gst-xmllaunch
GST_LOADSAVE_SRC_V = \
	gst-complete-@GST_MAJORMINOR@ \
	gst-compprep-@GST_MAJORMINOR@ \
	gst-xmllaunch-@GST_MAJORMINOR@
endif

GST_OTHER_SRC = \
	gst-feedback \
	gst-inspect \
	gst-launch \
	gst-md5sum \
	gst-typefind \
	gst-xmlinspect
GST_OTHER_SRC_V = \
	gst-inspect-@GST_MAJORMINOR@ \
	gst-launch-@GST_MAJORMINOR@ \
	gst-md5sum-@GST_MAJORMINOR@ \
	gst-typefind-@GST_MAJORMINOR@ \
	gst-xmlinspect-@GST_MAJORMINOR@

### so all of the programs we want to build
bin_PROGRAMS = \
	$(GST_REGISTRY_SRC) $(GST_REGISTRY_SRC_V) \
	$(GST_LOADSAVE_SRC) $(GST_LOADSAVE_SRC_V) \
	$(GST_OTHER_SRC) $(GST_OTHER_SRC_V)
bin_SCRIPTS = gst-feedback-@GST_MAJORMINOR@

# make sure each versioned tool has the right source file and flags
if !GST_DISABLE_REGISTRY
gst_register_@GST_MAJORMINOR@_SOURCES = gst-register.c
gst_register_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_register_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
endif
if !GST_DISABLE_LOADSAVE
gst_complete_@GST_MAJORMINOR@_SOURCES = gst-complete.c
gst_complete_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_complete_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_compprep_@GST_MAJORMINOR@_SOURCES = gst-compprep.c
gst_compprep_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_compprep_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_xmllaunch_@GST_MAJORMINOR@_SOURCES = gst-launch.c
gst_xmllaunch_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_xmllaunch_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
endif
gst_inspect_@GST_MAJORMINOR@_SOURCES = gst-inspect.c
gst_inspect_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_inspect_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_launch_@GST_MAJORMINOR@_SOURCES = gst-launch.c
gst_launch_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_launch_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_md5sum_@GST_MAJORMINOR@_SOURCES = gst-md5sum.c
gst_md5sum_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_md5sum_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_typefind_@GST_MAJORMINOR@_SOURCES = gst-typefind.c
gst_typefind_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_typefind_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)
gst_xmlinspect_@GST_MAJORMINOR@_SOURCES = gst-xmlinspect.c
gst_xmlinspect_@GST_MAJORMINOR@_CFLAGS = $(GST_CFLAGS)
gst_xmlinspect_@GST_MAJORMINOR@_LDFLAGS = $(GST_LIBS)

gst-feedback-@GST_MAJORMINOR@: gst-feedback-m.m
	cp $(srcdir)/gst-feedback-m.m $@
	chmod +x $@

# make sure each unversioned tool comes from gst-run.c
if !GST_DISABLE_REGISTRY
gst_register_SOURCES = gst-run.c
endif
if !GST_DISABLE_LOADSAVE
gst_complete_SOURCES = gst-run.c
gst_compprep_SOURCES = gst-run.c
gst_xmllaunch_SOURCES = gst-run.c
endif
gst_feedback_SOURCES = gst-run.c
gst_inspect_SOURCES = gst-run.c
gst_launch_SOURCES = gst-run.c
gst_md5sum_SOURCES = gst-run.c
gst_typefind_SOURCES = gst-run.c
gst_xmlinspect_SOURCES = gst-run.c

gst_inspect_@GST_MAJORMINOR@_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

gst_xmlinspect_@GST_MAJORMINOR@_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

# CFLAGS and libs for nonversioned frontend binaries
AM_CFLAGS = $(GLIB_ONLY_CFLAGS) $(POPT_CFLAGS)
LDADD = $(GLIB_ONLY_LIBS) $(POPT_LIBS)

### man pages we want to install
if GST_DISABLE_REGISTRY
GST_REGISTRY_MAN=
else
GST_REGISTRY_MAN = gst-register-@GST_MAJORMINOR@.1
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_MAN=
else
GST_LOADSAVE_MAN = \
	gst-complete-@GST_MAJORMINOR@.1 \
	gst-compprep-@GST_MAJORMINOR@.1 \
	gst-xmllaunch-@GST_MAJORMINOR@.1
endif

GST_OTHER_MAN = \
	gst-feedback-@GST_MAJORMINOR@.1 \
	gst-inspect-@GST_MAJORMINOR@.1 \
	gst-launch-@GST_MAJORMINOR@.1 \
	gst-md5sum-@GST_MAJORMINOR@.1 \
	gst-typefind-@GST_MAJORMINOR@.1
#	gst-xmlinspect-@GST_MAJORMINOR@.1

manpages = $(GST_REGISTRY_MAN) $(GST_LOADSAVE_MAN) $(GST_OTHER_MAN)

CLEANFILES = $(manpages) $(bin_SCRIPTS)
man_MANS = $(manpages)

EXTRA_DIST = \
	gst-complete.1.in \
	gst-compprep.1.in \
	gst-feedback.1.in \
	gst-inspect.1.in \
	gst-launch.1.in \
	gst-md5sum.1.in \
	gst-register.1.in \
	gst-typefind.1.in \
	gst-xmllaunch.1.in \
	gst-feedback-m.m

%-@GST_MAJORMINOR@.1: %.1.in
	sed \
		-e s,gst-complete,gst-complete-@GST_MAJORMINOR@,g \
		-e s,gst-compprep,gst-compprep-@GST_MAJORMINOR@,g \
		-e s,gst-feedback,gst-feedback-@GST_MAJORMINOR@,g \
		-e s,gst-inspect,gst-inspect-@GST_MAJORMINOR@,g \
		-e s,gst-launch,gst-launch-@GST_MAJORMINOR@,g \
		-e s,gst-md5sum,gst-md5sum-@GST_MAJORMINOR@,g \
		-e s,gst-register,gst-register-@GST_MAJORMINOR@,g \
		-e s,gst-typefind,gst-typefind-@GST_MAJORMINOR@,g \
		-e s,gst-xmllaunch,gst-xmllaunch-@GST_MAJORMINOR@,g \
		$< >$@

# developer helper tools, not meant for installation
noinst_SCRIPTS = gst-indent
