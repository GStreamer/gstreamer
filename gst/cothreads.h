/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __COTHREADS_H__
#define __COTHREADS_H__

#include <glib.h>
#include <setjmp.h>
#include <pthread.h>

#define COTHREAD_STACKSIZE 8192
#define COTHREAD_MAXTHREADS 16
#define STACK_SIZE 0x200000

#ifndef CURRENT_STACK_FRAME
#define CURRENT_STACK_FRAME  ({ char __csf; &__csf; })
#endif /* CURRENT_STACK_FRAME */

typedef struct _cothread_state 		cothread_state;
typedef struct _cothread_context 	cothread_context;

typedef int (*cothread_func) (int argc,char **argv);

#define COTHREAD_STARTED	0x01

struct _cothread_state {
  cothread_context *ctx;
  int threadnum;

  cothread_func func;
  int argc;
  char **argv;

  int flags;
  int *sp;
  int *top_sp;
  int *pc;
  jmp_buf jmp;
};

struct _cothread_context {
  cothread_state *threads[COTHREAD_MAXTHREADS];
  int nthreads;
  int current;
  GHashTable *data;
};

cothread_context*		cothread_init();
cothread_state*			cothread_create		(cothread_context *ctx);
void 				cothread_setfunc	(cothread_state *thread, cothread_func func, 
						         int argc, char **argv);
int				cothread_getcurrent	(void);
void 				cothread_switch		(cothread_state *thread);
void 				cothread_set_data	(cothread_state *thread, gchar *key, gpointer data);
gpointer 			cothread_get_data 	(cothread_state *thread, gchar *key);

cothread_state*			cothread_main		(cothread_context *ctx);

#endif /* __COTHREAD_H__ */
