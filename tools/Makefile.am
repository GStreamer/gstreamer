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

manpages = gst-launch.1 gst-md5sum.1 gst-register.1 gst-inspect.1 \
	gst-complete.1 gst-compprep.1 gst-xmllaunch.1 \
	gst-feedback.1

CLEANFILES = $(manpages)
man_MANS = $(manpages)

EXTRA_DIST = gst-launch.1.in gst-md5sum.1.in gst-register.1.in gst-inspect.1.in \
	gst-complete.1.in gst-compprep.1.in gst-xmllaunch.1.in \
	gst-feedback.1.in
EXTRA_DIST += gst-feedback

# make sure each versioned tool has the right source file
if !GST_DISABLE_REGISTRY
gst_register_@GST_MAJORMINOR@_SOURCES = gst-register.c
endif
if !GST_DISABLE_LOADSAVE
gst_complete_@GST_MAJORMINOR@_SOURCES = gst-complete.c
gst_compprep_@GST_MAJORMINOR@_SOURCES = gst-compprep.c
gst_xmllaunch_@GST_MAJORMINOR@_SOURCES = gst-launch.c
endif
gst_inspect_@GST_MAJORMINOR@_SOURCES = gst-inspect.c
gst_launch_@GST_MAJORMINOR@_SOURCES = gst-launch.c
gst_md5sum_@GST_MAJORMINOR@_SOURCES = gst-md5sum.c
gst_typefind_@GST_MAJORMINOR@_SOURCES = gst-typefind.c
gst_xmlinspect_@GST_MAJORMINOR@_SOURCES = gst-inspect.c

# make sure each unversioned tool comes from gst-run.c
if !GST_DISABLE_REGISTRY
gst_register_SOURCES = gst-run.c
endif
if !GST_DISABLE_LOADSAVE
gst_complete_SOURCES = gst-run.c
gst_compprep_SOURCES = gst-run.c
gst_xmllaunch_SOURCES = gst-run.c
endif
gst_inspect_SOURCES = gst-run.c
gst_launch_SOURCES = gst-run.c
gst_md5sum_SOURCES = gst-run.c
gst_typefind_SOURCES = gst-run.c
gst_xmlinspect_SOURCES = gst-run.c

# set correct compile and link flags for all, as well as for specific
AM_CFLAGS = $(GST_CFLAGS)
AM_LDFLAGS = $(GST_LIBS)

gst_inspect_@GST_MAJORMINOR@_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

gst_xmlinspect_@GST_MAJORMINOR@_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la


bin_SCRIPTS = gst-feedback

SUFFIXES = .1 .1.in
.1.in.1:
	sed \
		-e s,gst.-complete,`echo gst-complete | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-compprep,`echo gst-compprep | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-feedback,`echo gst-feedback | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-inspect,`echo gst-inspect | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-launch,`echo gst-launch | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-md5sum,`echo gst-md5sum | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-register,`echo gst-register | sed $(program_transform_name) | sed s/-/\\-/`,g \
		-e s,gst.-xmllaunch,`echo gst-xmllaunch | sed $(program_transform_name) | sed s/-/\\-/`,g \
		$< >$@

