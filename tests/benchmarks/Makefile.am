if !GST_DISABLE_GST_DEBUG
TRACER_BENCH = tracerserialize
else
TRACER_BENCH =
endif

noinst_PROGRAMS = \
        caps \
        capsnego \
        complexity \
        controller \
        init \
        mass-elements \
        gstpollstress \
        gstpoolstress \
        gstclockstress	\
        gstbufferstress \
        $(TRACER_BENCH)

LDADD = $(GST_OBJ_LIBS)
AM_CFLAGS = $(GST_OBJ_CFLAGS)

controller_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs
controller_LDADD = $(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la $(LDADD)

