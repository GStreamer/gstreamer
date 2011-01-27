bin_PROGRAMS = ges-launch-@GST_MAJORMINOR@
AM_CFLAGS =  -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la $(GST_PBUTILS_LIBS) $(GST_LIBS) 

ges_launch_@GST_MAJORMINOR@_SOURCES = ges-launch.c

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT ges_launch -:EXECUTABLE ges-launch \
	 -:TAGS eng \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(ges_launch_@GST_MAJORMINOR@_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(AM_CFLAGS) \
	 -:LDFLAGS -lges-@GST_MAJORMINOR@ $(GST_PBUTILS_LIBS) $(GST_LIBS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
