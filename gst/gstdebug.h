/* Gnome-Streamer
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


#ifndef __GSTDEBUG_H__
#define __GSTDEBUG_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

/* for include files that make too much noise normally */
#ifdef GST_DEBUG_FORCE_DISABLE
#undef GST_DEBUG_ENABLED
#endif

/* for applications that really really want all the noise */
#ifdef GST_DEBUG_FORCE_ENABLE
#define GST_DEBUG_ENABLED
#endif


#define GST_DEBUG_PREFIX(format,args...) \
"DEBUG(%d:%d)" __PRETTY_FUNCTION__ ":%d" format , getpid() , cothread_getcurrent() , __LINE__ , ## args


/* fallback, this should probably be a 'weak' symbol or something */
G_GNUC_UNUSED static gchar *_debug_string = NULL;


/**********************************************************************
 * The following is a DEBUG_ENTER implementation that will wrap the
 * function it sits at the head of.  It removes the need for a
 * DEBUG_LEAVE call.  However, it segfaults whenever it gets anywhere
 * near cothreads.  We will not use it for the moment.
 */
typedef void (*_debug_function_f)();
G_GNUC_UNUSED static gchar *_debug_string_pointer = NULL;
G_GNUC_UNUSED static GModule *_debug_self_module = NULL;

#define _DEBUG_ENTER_BUILTIN(format,args...)						\
  static int _debug_in_wrapper = 0;							\
  gchar *_debug_string = ({								\
    if (!_debug_in_wrapper) {								\
      void *_return_value;								\
      gchar *_debug_string;								\
      _debug_function_f function;							\
      void *_function_args = __builtin_apply_args();					\
      _debug_in_wrapper = 1;								\
      _debug_string = g_strdup_printf(GST_DEBUG_PREFIX(""));				\
      _debug_string_pointer = _debug_string;						\
      fprintf(stderr,"%s: entered " __PRETTY_FUNCTION__ format "\n" , _debug_string , ## args ); \
      if (_debug_self_module == NULL) _debug_self_module = g_module_open(NULL,0);	\
      g_module_symbol(_debug_self_module,__FUNCTION__,(gpointer *)&function);		\
      _return_value = __builtin_apply(function,_function_args,64);			\
      fprintf(stderr,"%s: left " __PRETTY_FUNCTION__ format "\n" , _debug_string , ## args ); \
      g_free(_debug_string);								\
      __builtin_return(_return_value);							\
    } else {										\
      _debug_in_wrapper = 0;								\
    }											\
    _debug_string_pointer;								\
  });

/* WARNING: there's a gcc CPP bug lurking in here.  The extra space before the ##args	*
 * somehow make the preprocessor leave the _debug_string. If it's removed, the		*
 * _debug_string somehow gets stripped along with the ##args, and that's all she wrote. */
#define _DEBUG_BUILTIN(format,args...)				\
  if (_debug_string != (void *)-1) {				\
    if (_debug_string)						\
      fprintf(stderr,"%s: " format , _debug_string , ## args);	\
    else							\
      fprintf(stderr,GST_DEBUG_PREFIX(": " format , ## args));	\
  }

#ifdef GST_DEBUG_ENABLED
#define DEBUG(format,args...) \
  (_debug_string != NULL) ? \
    fprintf(stderr,GST_DEBUG_PREFIX("%s: "format , _debug_string , ## args )) : \
    fprintf(stderr,GST_DEBUG_PREFIX(": "format , ## args ))
#define DEBUG_ENTER(format, args...) \
  fprintf(stderr,GST_DEBUG_PREFIX(format": entering\n" , ## args ))
#define DEBUG_SET_STRING(format, args...) \
  gchar *_debug_string = g_strdup_printf(format , ## args )
#define DEBUG_ENTER_STRING DEBUG_ENTER("%s",_debug_string)
#define DEBUG_LEAVE(format, args...) \
  if (_debug_string != NULL) g_free(_debug_string),\
fprintf(stderr,GST_DEBUG_PREFIX(format": leaving\n" , ## args ))
#else
#define DEBUG(format, args...)
#define DEBUG_ENTER(format, args...)
#define DEBUG_LEAVE(format, args...)
#define DEBUG_SET_STRING(format, args...)
#define DEBUG_ENTER_STRING
#endif



/********** some convenience macros for debugging **********/
#define GST_DEBUG_PAD_NAME(pad) \
  ((pad)->parent != NULL) ? gst_element_get_name(GST_ELEMENT((pad)->parent)) : "''", gst_pad_get_name(pad)

#endif /* __GST_H__ */
