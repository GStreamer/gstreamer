/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * cothreads.h: Header for cothreading routines
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

typedef struct _cothread_state		cothread_state;
typedef struct _cothread_context	cothread_context;

typedef int (*cothread_func) (int argc,char **argv);

#define COTHREAD_STARTED	0x01
#define COTHREAD_DESTROYED	0x02

struct _cothread_state {
  cothread_context 	*ctx;
  int			 cothreadnum;
  gpointer		 priv;

  cothread_func		 func;
  int			 argc;
  char		       **argv;

  int			 flags;
  void			*sp;
  jmp_buf		 jmp;
  void			*stack_base;
  unsigned long		 stack_size;

  int			 magic_number;
};


cothread_context*		cothread_context_init   	(void);
void				cothread_context_free		(cothread_context *ctx);
void				cothread_context_set_data	(cothread_state *cothread, 
								 gchar *key, gpointer data);
gpointer			cothread_context_get_data	(cothread_state *cothread, gchar *key);

cothread_state*			cothread_create			(cothread_context *ctx);
void				cothread_free			(cothread_state *cothread);
void				cothread_setfunc		(cothread_state *cothread, cothread_func func,
						        	 int argc, char **argv);
void				cothread_stop			(cothread_state *cothread);

void				cothread_switch			(cothread_state *cothread);
void				cothread_set_private		(cothread_state *cothread, 
								 gpointer data);
gpointer			cothread_get_private		(cothread_state *cothread);

void				cothread_lock			(cothread_state *cothread);
gboolean			cothread_trylock		(cothread_state *cothread);
void				cothread_unlock			(cothread_state *cothread);

cothread_state*			cothread_main			(cothread_context *ctx);
cothread_state*			cothread_current_main		(void);
cothread_state*			cothread_current		(void);

#endif /* __COTHREAD_H__ */
