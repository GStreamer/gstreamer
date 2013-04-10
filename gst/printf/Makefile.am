AM_CPPFLAGS = $(GLIB_CFLAGS) -DSTATIC=G_GNUC_INTERNAL $(WARNING_CFLAGS) $(PRINTF_CFLAGS)

# don't have a need for that and it's not portable so just ignore for now
AM_CPPFLAGS += -UHAVE_LONG_DOUBLE

# just use internal emulation for printing long longs for now
AM_CPPFLAGS += -UHAVE_LONG_LONG_FORMAT

# don't need any of this widechar stuff, so just disable it for now
AM_CPPFLAGS += -UHAVE_WCHAR_T -UHAVE_WCSLEN -UHAVE_WINT_T

# in case anyone wants to add anything else or undo some of the undefs
AM_CPPFLAGS += $(PRINTF_EXTRA_CFLAGS)

noinst_LTLIBRARIES = libgstprintf.la

libgstprintf_la_CFLAGS =
libgstprintf_la_SOURCES = 	\
	asnprintf.c \
	printf-args.c \
	printf-args.h \
	printf-parse.c \
	printf-parse.h \
	vasnprintf.c \
	vasnprintf.h \
	printf.c \
	printf.h \
	printf-extension.c \
	printf-extension.h \
	gst-printf.h

EXTRA_DIST = README
