/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * cothreads_compat.h: Compatibility macros between cothreads packages
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* use the old cothreads implementation in gst/cothreads.[ch] */
#if defined(_COTHREADS_OMEGA)
 
#include "../cothreads.h"

/* the name of this cothreads type */
#define COTHREADS_TYPE		omega
#define COTHREADS_NAME		"omega"
#define COTHREADS_NAME_CAPITAL	"Omega"

/* unify the structs 
 *
 * "cothread" and "cothread_context" need to be defined
 */
typedef cothread_state cothread;

/* define functions
 * the macros are prepended with "do_" 
 */
#define do_cothreads_init(x) 			/* NOP */

#define do_cothreads_stackquery(stack,size)	cothread_stackquery(stack,size)

#define do_cothread_switch(to)			cothread_switch(to)

#define do_cothread_create(new_cothread, context, func, argc, argv)	\
  G_STMT_START{                                                         \
    new_cothread = cothread_create (context);                           \
    if (new_cothread) {                                                 \
      cothread_setfunc (new_cothread, (func), (argc), (argv));          \
    }                                                                   \
  }G_STMT_END

#define do_cothread_setfunc(cothread, context, func, argc, argv)        \
  cothread_setfunc ((cothread), (func), (argc), (argv))
  
#define do_cothread_destroy(cothread)		cothread_free(cothread)

#define do_cothread_context_init()		(cothread_context_init ())
#define do_cothread_context_destroy(context)	cothread_context_free (context)
  
#define do_cothread_get_current(context)		(cothread_current())
#define do_cothread_get_main(context)		(cothread_current_main())
  
  
  
  
/* use the gthread-based cothreads implementation */
#elif defined(_COTHREADS_GTHREAD)

#include "gthread-cothreads.h"
  
  
/* bail out with an error if no cothreads package is defined */
#else
#error "No cothreads package defined"
#endif
