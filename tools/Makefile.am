if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
else
GST_REGISTRY_SRC = gst-register
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
else
GST_LOADSAVE_SRC = gst-compprep gst-xmllaunch gst-complete
endif

manpages = gst-launch.1 gst-md5sum.1 gst-register.1 gst-inspect.1 \
	gst-complete.1 gst-compprep.1 gst-xmllaunch.1 \
	gst-feedback.1

CLEANFILES = $(manpages)
man_MANS = $(manpages)
EXTRA_DIST = gst-launch.1.in gst-md5sum.1.in gst-register.1.in gst-inspect.1.in \
	gst-complete.1.in gst-compprep.1.in gst-xmllaunch.1.in \
	gst-feedback.1.in
EXTRA_DIST += gst-feedback

bin_PROGRAMS = 	gst-launch 		\
		$(GST_REGISTRY_SRC) 	\
		gst-inspect 		\
		gst-xmlinspect 		\
		gst-typefind 		\
		$(GST_LOADSAVE_SRC) 	\
		gst-md5sum 		

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

AM_CFLAGS = $(GST_CFLAGS)
AM_LDFLAGS = $(GST_LIBS)

gst_inspect_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

gst_xmlinspect_LDADD = ../libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

if !GST_DISABLE_REGISTRY
gst_xmllaunch_SOURCES = gst-launch.c
endif

