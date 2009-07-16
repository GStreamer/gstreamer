noinst_LTLIBRARIES	= libcheckinternal.la

EXTRA_DIST		= check.h.in

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

EXPORT_SYM	= exported.sym
$(EXPORT_SYM): check.h.in
	sed -n -e 's/^..*CK_EXPORT[[:space:]][[:space:]]*\([[:alnum:]_][[:alnum:]_]*\)..*$$/\1/p' @top_srcdir@/src/check.h.in > $@

libcheckinternal_la_SOURCES	= $(CFILES) $(HFILES)
libcheckinternal_la_LIBADD	= 


CLEANFILES	= $(EXPORT_SYM)

