/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2001 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  pth_acdef.h Autoconf defines
*/

#ifndef _PTH_ACDEF_H_
#define _PTH_ACDEF_H_

@TOP@

/* the custom Autoconf defines */
#undef HAVE_SIG_ATOMIC_T
#undef HAVE_PID_T
#undef HAVE_STACK_T
#undef HAVE_SIZE_T
#undef HAVE_SSIZE_T
#undef HAVE_SOCKLEN_T
#undef HAVE_NFDS_T
#undef HAVE_OFF_T
#undef HAVE_GETTIMEOFDAY_ARGS1
#undef HAVE_STRUCT_TIMESPEC
#undef HAVE_SYS_READ
#undef HAVE_POLLIN
#undef HAVE_SS_SP
#undef HAVE_SS_BASE
#undef HAVE_LONGLONG
#undef HAVE_LONGDOUBLE
#undef PTH_DEBUG
#undef PTH_NSIG
#undef PTH_MCTX_MTH_use
#undef PTH_MCTX_DSP_use
#undef PTH_MCTX_STK_use
#undef PTH_STACKGROWTH
#undef PTH_DMALLOC

@BOTTOM@

#endif /* _PTH_ACDEF_H_ */

