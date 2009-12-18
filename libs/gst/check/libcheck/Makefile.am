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
	check_str.c

HFILES =\
	check.h		\
	check_error.h	\
	check_impl.h	\
	check_list.h	\
	check_log.h	\
	check_msg.h	\
	check_pack.h	\
	check_print.h	\
	check_str.h

noinst_HEADERS = $(HFILES)

libcheckinternal_la_SOURCES	= $(CFILES) $(HFILES)

# define HAVE_PTHREAD here as well so we keep changes to the code to a minimum
if HAVE_PTHREAD_H
libcheckinternal_la_CFLAGS	= -D_GNU_SOURCE -DHAVE_PTHREAD
libcheckinternal_la_LIBADD	= -lpthread
else
libcheckinternal_la_CFLAGS	= -D_GNU_SOURCE
libcheckinternal_la_LIBADD	=
endif
