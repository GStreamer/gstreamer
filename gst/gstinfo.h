/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstinfo.h: 
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

#ifndef __GSTINFO_H__
#define __GSTINFO_H__

#include <stdio.h>
#include <gmodule.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "cothreads.h"


/**********************************************************************
 * DEBUG system
 **********************************************************************/

extern guint32 _gst_debug_categories;

/* for include files that make too much noise normally */
#ifdef GST_DEBUG_FORCE_DISABLE
#undef GST_DEBUG_ENABLED
#endif
/* for applications that really really want all the noise */
#ifdef GST_DEBUG_FORCE_ENABLE
#define GST_DEBUG_ENABLED
#endif

//#ifdef GST_DEBUG_ENABLED
#define GST_DEBUG_ENABLE_CATEGORIES 0xffffffff
//#else
//#define GST_DEBUG_ENABLE_CATEGORIES 0x00000000
//#endif

/* fallback, this should probably be a 'weak' symbol or something */
G_GNUC_UNUSED static gchar *_debug_string = NULL;

#define GST_DEBUG_PREFIX(format,args...) \
"DEBUG(%d:%d)" __PRETTY_FUNCTION__ ":%d" format , getpid() , cothread_getcurrent() , __LINE__ , ## args

#ifdef GST_DEBUG_ENABLED
#define GST_DEBUG(cat,format,args...) G_STMT_START{ \
  if (((1<<cat) & GST_DEBUG_ENABLE_CATEGORIES) && \
      ((1<<cat) & _gst_debug_categories)) \
    (_debug_string != NULL) ? \
      fprintf(stderr,GST_DEBUG_PREFIX("%s: "format , _debug_string , ## args )) : \
      fprintf(stderr,GST_DEBUG_PREFIX(": "format , ## args )); \
}G_STMT_END

#define GST_DEBUG_NOPREFIX(cat,format,args...) G_STMT_START{ \
  if (((1<<cat) & GST_DEBUG_ENABLE_CATEGORIES) && \
      ((1<<cat) & _gst_debug_categories)) \
    fprintf(stderr,format , ## args ); \
}G_STMT_END

#define GST_DEBUG_ENTER(format, args...) G_STMT_START{ \
  if (((1<<31) & GST_DEBUG_ENABLE_CATEGORIES) && \
      ((1<<31) & _gst_debug_categories)) \
    fprintf(stderr,GST_DEBUG_PREFIX(format": entering\n" , ## args )); \
}G_STMT_END

// FIXME FIXME FIXME this leaks like crazy
#define GST_DEBUG_SET_STRING(format, args...) \
  gchar *_debug_string = g_strdup_printf(format , ## args )

#define GST_DEBUG_ENTER_STRING GST_DEBUG_ENTER("%s",_debug_string)

#define GST_DEBUG_LEAVE(format, args...) G_STMT_START{ \
  if (((1<<31) & GST_DEBUG_ENABLE_CATEGORIES) && \
      ((1<<31) & _gst_debug_categories)) \
    if (_debug_string != NULL) g_free(_debug_string),\
      fprintf(stderr,GST_DEBUG_PREFIX(format": leaving\n" , ## args )); \
}G_STMT_END

#define GST_DEBUG_LEAVE_STRING GST_DEBUG_LEAVE("%s",_debug_string)

#else
#define GST_DEBUG(format, args...)
#define GST_DEBUG_NOPREFIX(format, args...)
#define GST_DEBUG_ENTER(format, args...)
#define GST_DEBUG_LEAVE(format, args...)
#define GST_DEBUG_SET_STRING(format, args...)
#define GST_DEBUG_ENTER_STRING
#endif



/********** some convenience macros for debugging **********/
#define GST_DEBUG_PAD_NAME(pad) \
  (GST_OBJECT_PARENT(pad) != NULL) ? \
  GST_OBJECT_NAME (GST_OBJECT_PARENT(pad)) : \
  "''", GST_OBJECT_NAME (pad)



/********** function pointer stuff **********/
extern GHashTable *__gst_function_pointers;

#ifdef GST_DEBUG_ENABLED
#define GST_DEBUG_FUNCPTR(ptr) _gst_debug_register_funcptr((void *)(ptr), #ptr)
#define GST_DEBUG_FUNCPTR_NAME(ptr) _gst_debug_nameof_funcptr((void *)ptr)
#else
#define GST_DEBUG_FUNCPTR(ptr) (ptr)
#define GST_DEBUG_FUNCPTR_NAME(ptr) ""
#endif

static inline void *
_gst_debug_register_funcptr (void *ptr, gchar *ptrname) 
{
  if (!__gst_function_pointers) __gst_function_pointers = g_hash_table_new(g_direct_hash,g_direct_equal);
  if (!g_hash_table_lookup(__gst_function_pointers,ptr))
    g_hash_table_insert(__gst_function_pointers,ptr,ptrname);
  return ptr;
}

static inline gchar *
_gst_debug_nameof_funcptr (void *ptr) 
{
  gchar *ptrname = (gchar*)( __gst_function_pointers ? g_hash_table_lookup(__gst_function_pointers,ptr) : NULL );
// FIXME this must go away, it's a major leak
  if (!ptrname) return g_strdup_printf("%p",ptr);
  else return ptrname;
}


/**********************************************************************
 * The following is a DEBUG_ENTER implementation that will wrap the
 * function it sits at the head of.  It removes the need for a
 * DEBUG_LEAVE call.  However, it segfaults whenever it gets anywhere
 * near cothreads.  We will not use it for the moment.
 *
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

* WARNING: there's a gcc CPP bug lurking in here.  The extra space before the ##args	*
 * somehow make the preprocessor leave the _debug_string. If it's removed, the		*
 * _debug_string somehow gets stripped along with the ##args, and that's all she wrote. *
#define _DEBUG_BUILTIN(format,args...)				\
  if (_debug_string != (void *)-1) {				\
    if (_debug_string)						\
      fprintf(stderr,"%s: " format , _debug_string , ## args);	\
    else							\
      fprintf(stderr,GST_DEBUG_PREFIX(": " format , ## args));	\
  }

*/



/**********************************************************************
 * INFO system
 **********************************************************************/

typedef void (*GstInfoHandler) (gint category,gchar *file,gchar *function,
                                gint line,gchar *debug_string,
                                void *element,gchar *string);

void gst_default_info_handler (gint category,gchar *file,gchar *function,
                               gint line,gchar *debug_string,
                               void *element,gchar *string);

extern GstInfoHandler _gst_info_handler;
extern guint32 _gst_info_categories;

/* for include files that make too much noise normally */
#ifdef GST_INFO_FORCE_DISABLE
#undef GST_INFO_ENABLED
#endif
/* for applications that really really want all the noise */
#ifdef GST_INFO_FORCE_ENABLE
#define GST_INFO_ENABLED
#endif

#ifdef GST_INFO_ENABLED
#define GST_INFO(cat,format,args...) G_STMT_START{ \
  if ((1<<cat) & _gst_info_categories) \
    _gst_info_handler(cat,__FILE__,__PRETTY_FUNCTION__,__LINE__,_debug_string, \
                      NULL,g_strdup_printf( format , ## args )); \
}G_STMT_END

#define GST_INFO_ELEMENT(cat,element,format,args...) G_STMT_START{ \
  if ((1<<cat) & _gst_info_categories) \
    _gst_info_handler(cat,__FILE__,__PRETTY_FUNCTION__,__LINE__,_debug_string, \
                      element,g_strdup_printf( format , ## args )); \
}G_STMT_END

#else
#define GST_INFO(cat,format,args...) 
#define GST_INFO_ELEMENT(cat,element,format,args...)
#endif


void		gst_info_set_categories		(guint32 categories);
guint32		gst_info_get_categories		(void);
void		gst_info_enable_category	(gint category);
void		gst_info_disable_category	(gint category);

void		gst_debug_set_categories	(guint32 categories);
guint32		gst_debug_get_categories	(void);
void		gst_debug_enable_category	(gint category);
void		gst_debug_disable_category	(gint category);

const gchar *	gst_get_category_name	(gint category);


enum {
  GST_CAT_GST_INIT = 0,		// Library initialization
  GST_CAT_COTHREADS,		// Cothread creation, etc.
  GST_CAT_COTHREAD_SWITCH,	// Cothread switching
  GST_CAT_AUTOPLUG,		// Successful autoplug results
  GST_CAT_AUTOPLUG_ATTEMPT,	// Attempted autoplug operations
  GST_CAT_PARENTAGE,		// GstBin parentage issues
  GST_CAT_STATES,		// State changes and such
  GST_CAT_PLANNING,		// Plan generation
  GST_CAT_SCHEDULING,		// Schedule construction
  GST_CAT_DATAFLOW,		// Events during actual data movement
  GST_CAT_BUFFER,		// Buffer creation/destruction
  GST_CAT_CAPS,			// Capabilities matching
  GST_CAT_CLOCK,		// Clocking
  GST_CAT_ELEMENT_PADS,		// Element pad management
  GST_CAT_ELEMENTFACTORY,	// Elementfactory stuff
  GST_CAT_PADS,			// Pad creation/connection
  GST_CAT_PIPELINE,		// Pipeline stuff
  GST_CAT_PLUGIN_LOADING,	// Plugin loading
  GST_CAT_PLUGIN_ERRORS,	// Errors during plugin loading
  GST_CAT_PROPERTIES,		// Properties
  GST_CAT_THREAD,		// Thread creation/management
  GST_CAT_TYPES,		// Typing
  GST_CAT_XML,			// XML load/save of everything

  GST_CAT_MAX_CATEGORY,
};




/**********************************************************************
 * ERROR system
 **********************************************************************/

typedef void (*GstErrorHandler) (gchar *file,gchar *function,
                                 gint line,gchar *debug_string,
                                 void *element,void *object,gchar *string);

void gst_default_error_handler (gchar *file,gchar *function,
                                gint line,gchar *debug_string,
                                void *element,void *object,gchar *string);

extern GstErrorHandler _gst_error_handler;

#define GST_ERROR(element,format,args...) \
  _gst_error_handler(__FILE__,__PRETTY_FUNCTION__,__LINE__,_debug_string, \
                     element,NULL,g_strdup_printf( format , ## args ))

#define GST_ERROR_OBJECT(element,object,format,args...) \
  _gst_error_handler(__FILE__,__PRETTY_FUNCTION__,__LINE__,_debug_string, \
                     element,object,g_strdup_printf( format , ## args ))



#endif /* __GSTINFO_H__ */
