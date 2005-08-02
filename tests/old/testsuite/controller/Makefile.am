include ../Rules

tests_pass = interpolator
tests_fail = 
tests_ignore =

interpolator_SOURCES = interpolator.c
interpolator_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs
interpolator_LDFLAGS = $(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la
