noinst_PROGRAMS = \
        caps \
        capsnego \
        complexity \
        controller \
        init \
        mass-elements \
        gstpollstress \
        gstclockstress	\
	gstbufferstress

LDADD = $(GST_OBJ_LIBS)
AM_CFLAGS = $(GST_OBJ_CFLAGS)

controller_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs
controller_LDADD = $(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la $(LDADD)

