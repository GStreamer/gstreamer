##
##  NGPT - Next Generation Threading
##  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
##  Portions Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
##
##  This file is part of NGPT, a non-preemptive thread scheduling
##  library which can be found at http://www.ibm.com/developer
##
##  This library is free software; you can redistribute it and/or
##  modify it under the terms of the GNU Lesser General Public
##  License as published by the Free Software Foundation; either
##  version 2.1 of the License, or (at your option) any later version.
##
##  This library is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
##  Lesser General Public License for more details.
##
##  You should have received a copy of the GNU Lesser General Public
##  License along with this library; if not, write to the Free Software
##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
##  USA.
##
##  Makefile.am: NGPT Makefile.in hacked into an automake file by wingo
##  <wingo@pobox.com>
##
                             # ``UNIX -- where you can do anything
                             #   in two keystrokes, or less...''

noinst_LTLIBRARIES = libpth-mctx.la

libpth_mctx_la_SOURCES = pth_mctx.c pth_p.h pth_vers.c pth.h pth_acdef.h pth_acmac.h

noinst_PROGRAMS = test-pth test-pth-pthreads

test_pth_LDADD = libpth-mctx.la
test_pth_pthreads_LDADD = libpth-mctx.la -lpthread

#pth_mctx_a_DEPENDENCIES = shtool

BUILT_SOURCES = pth_p.h

HSRCS = $(srcdir)/pth_debug.c $(srcdir)/pth_errno.c $(srcdir)/pth_mctx.c

#   build the private shared header file
pth_p.h: $(srcdir)/pth_p.h.in $(HSRCS)
	$(srcdir)/shtool scpp -o pth_p.h -t $(srcdir)/pth_p.h.in -Dcpp -Cintern -M '==#==' $(HSRCS)

#shtool:
#	shtoolize -o shtool scpp
