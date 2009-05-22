noinst_PROGRAMS = \
        caps \
        complexity \
        controller \
        init \
        mass-elements \
        gstpollstress \
        gstclockstress

LDADD = $(GST_OBJ_LIBS)
AM_CFLAGS = $(GST_OBJ_CFLAGS)

controller_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs
controller_LDFLAGS = $(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la

