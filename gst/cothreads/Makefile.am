##  Makefile.am: NGPT Makefile.in hacked into an automake file by wingo
##  <wingo@pobox.com>
##
                             # ``UNIX -- where you can do anything
                             #   in two keystrokes, or less...''

noinst_LTLIBRARIES = libpth-mctx.la

libpth_mctx_la_SOURCES = pth_mctx.c pth_p.h pth_vers.c pth.h pth_acdef.h pth_acmac.h

noinst_PROGRAMS = test-pth test-pth-pthreads test-pthreads test-pth-pthreads2
noinst_HEADERS = linuxthreads.h

test_pth_LDADD = libpth-mctx.la
test_pth_pthreads_LDADD = libpth-mctx.la -lpthread
test_pthreads_LDADD = -lpthread
test_pth_pthreads2_LDADD = -lpthread libpth-mctx.la

BUILT_SOURCES = pth_p.h

EXTRA_DIST = ltmain.sh mkinstalldirs config.guess config.sub install-sh missing

HSRCS = $(srcdir)/pth_debug.c $(srcdir)/pth_errno.c $(srcdir)/pth_mctx.c

pth_p.h: $(srcdir)/pth_p.h.in $(HSRCS)
	$(srcdir)/shtool scpp -o pth_p.h -t $(srcdir)/pth_p.h.in -Dcpp -Cintern -M '==#==' $(HSRCS)

#pth_mctx_a_DEPENDENCIES = shtool
#shtool:
#	shtoolize -o shtool scpp
