noinst_LTLIBRARIES	= libcheckinternal.la

EXTRA_DIST = check.h.in

CFILES =\
	check.c		\
	check_error.c	\
	check_list.c	\
	check_log.c	\
	check_msg.c	\
	check_pack.c	\
	check_print.c	\
	check_run.c	\
	check_str.c	\
	libcompat.c

if !HAVE_ALARM
CFILES += alarm.c
endif

if !HAVE_CLOCK_GETTIME
CFILES += clock_gettime.c
endif

if !HAVE_STRSIGNAL
CFILES += strsignal.c
endif

if !HAVE_TIMER_CREATE_SETTIME_DELETE
CFILES +=\
	timer_create.c	\
	timer_settime.c	\
	timer_delete.c
endif

HFILES =\
	check_error.h	\
	check_impl.h	\
	check_list.h	\
	check_log.h	\
	check_msg.h	\
	check_pack.h	\
	check_print.h	\
	check_str.h	\
	libcompat.h

noinst_HEADERS = $(HFILES)

libcheckinternal_la_SOURCES	= $(CFILES) $(HFILES)

libcheckinternal_la_CFLAGS	= -I$(top_builddir)/libs/gst/check
libcheckinternal_la_LIBADD	=

# define HAVE_PTHREAD here as well so we keep changes to the code to a minimum
if HAVE_PTHREAD
libcheckinternal_la_CFLAGS	+= $(PTHREAD_CFLAGS) -D_GNU_SOURCE -DHAVE_PTHREAD
libcheckinternal_la_LIBADD	+= $(PTHREAD_LIBS)
else
libcheckinternal_la_CFLAGS	+= -D_GNU_SOURCE
endif
