/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstscheduler.c: Default scheduling code for most cases
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
#if defined(_COTHREADS_BASIC)
 
#include "../cothreads.h"

/* the name of this cothreads type */
#define COTHREADS_NAME		"basic"
#define COTHREADS_NAME_CAPITAL	"Basic"

/* unify the structs 
 *
 * "cothread" and "cothread_context" need to vbe defined
 */
typedef cothread_state cothread;

/* define functions
 * the macros are prepended with "do_" 
 */
#define do_cothreads_init(x) 			/* NOP */

#define do_cothreads_stackquery(stack,size)	FALSE

#define do_cothread_switch(to)			cothread_switch(to)

#define do_cothread_create(new_thread, context, func, argc, argv)	\
  G_STMT_START{	\
    new_thread = cothread_create (context);	\
    if (new_thread) {	\
      cothread_setfunc (new_thread, (func), (argc), (argv));	\
    }\
  }G_STMT_END

#define do_cothread_setfunc(cothread, context, func, argc, argv) \
  cothread_setfunc ((cothread), (func), (argc), (argv))
  
#define do_cothread_destroy(cothread)		cothread_free(cothread)

#define do_cothread_context_init()		(cothread_context_init ())
#define do_cothread_context_destroy(context)	cothread_context_free (context)
  
#define do_cothread_lock(cothread)		cothread_lock(cothread)
#define do_cothread_unlock(cothread)		cothread_unlock(cothread)

#define do_cothread_get_current()		(cothread_current())
#define do_cothread_get_main(context)		(cothread_current_main())
  
  
  
  
/* use the new cothreads implementation in libs/ext/cothreads */
#elif defined(_COTHREADS_STANDARD)

#include <cothreads/cothreads.h>
#include <errno.h>

/* the name of this cothreads */
#define COTHREADS_NAME		"standard"
#define COTHREADS_NAME_CAPITAL	"Standard"

/* unify the structs 
 *
 * "cothread" and "cothread_context" need to vbe defined
 */
typedef cothread cothread_context;

/* define functions
 * the macros are prepended with "do_" 
 */
#define do_cothreads_init(x) G_STMT_START{	\
    if (!cothreads_initialized())	\
      cothreads_init(0x0200000, 16);	\
  }G_STMT_END

#define do_cothreads_stackquery(stack,size)	\
  cothreads_alloc_thread_stack (stack, size)

#define do_cothread_switch(to)	G_STMT_START{	\
  cothread *from = cothread_self ();	\
  if (from == (to)) {	\
    GST_DEBUG (GST_CAT_COTHREAD_SWITCH, "trying to switch to the same cothread (%p), not allowed",	\
              (to));	\
    g_warning ("trying to switch to the same cothread, not allowed");	\
  } else {	\
    GST_INFO (GST_CAT_COTHREAD_SWITCH, "switching from cothread %p to cothread %p",	\
	      from, (to));	\
    cothread_switch (from, (to));	\
    GST_INFO (GST_CAT_COTHREAD_SWITCH, "we're in cothread %p now", from);	\
  }	\
}G_STMT_END

#define do_cothread_create(new_thread, context, func, argc, argv)	\
  G_STMT_START{	\
    new_thread = cothread_create ((func), 0, (void**) (argv), (context));	\
  }G_STMT_END
  
#define do_cothread_setfunc(cothread, context, func, argc, argv) \
  cothread_setfunc ((cothread), (func), (argc), (void **) (argv), (context))

#define do_cothread_destroy(cothread)		cothread_destroy(cothread)
  
#define do_cothread_context_init()		(cothread_create (NULL, 0, NULL, NULL))
#define do_cothread_context_destroy(context)	cothread_destroy (context)
  
#define do_cothread_lock(cothread)		/* FIXME */
#define do_cothread_unlock(cothread)		/* FIXME */

#define do_cothread_get_current()		(cothread_self())
#define do_cothread_get_main(context)		(context)




/* bail out with an error if no cothreads package is defined */
#else
#error "No cothreads package defined"
#endif
