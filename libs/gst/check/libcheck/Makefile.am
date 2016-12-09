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
	libcompat/libcompat.c

if !HAVE_ALARM
CFILES += libcompat/alarm.c
endif

if !HAVE_CLOCK_GETTIME
CFILES += libcompat/clock_gettime.c
endif

if !HAVE_GETTIMEOFDAY
CFILES += libcompat/gettimeofday.c
endif

if !HAVE_LOCALTIME_R
CFILES += libcompat/localtime_r.c
endif

if !HAVE_STRSIGNAL
CFILES += libcompat/strsignal.c
endif

if !HAVE_STRDUP
CFILES += libcompat/strdup.c
endif

if !HAVE_GETLINE
CFILES += libcompat/getline.c
endif

if !HAVE_TIMER_CREATE_SETTIME_DELETE
CFILES +=\
	libcompat/timer_create.c	\
	libcompat/timer_settime.c	\
	libcompat/timer_delete.c
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
	libcompat/libcompat.h

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

# Don't want libcompat to think we don't have these and substitute replacements
# See libcompat/libcompat.h
libcheckinternal_la_CFLAGS	+= -DHAVE_SNPRINTF -DHAVE_VSNPRINTF -DHAVE_MALLOC -DHAVE_REALLOC
