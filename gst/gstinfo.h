/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gstinfo.h: debugging functions
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

#include <glib.h>
#include <glib-object.h>
#include <gst/gstatomic.h>
#include <gst/gstlog.h>
#include <gst/gstconfig.h>

G_BEGIN_DECLS
/*
 * GStreamer's debugging subsystem is an easy way to get information about what
 * the application is doing.
 * It is not meant for programming errors. Use GLibs methods (g_warning and so
 * on for that.
 */
/* log levels */
    typedef enum
{
  GST_LEVEL_NONE = 0,
  GST_LEVEL_ERROR,
  GST_LEVEL_WARNING,
  GST_LEVEL_INFO,
  GST_LEVEL_DEBUG,
  GST_LEVEL_LOG,
  /* add more */
  GST_LEVEL_COUNT
} GstDebugLevel;

/* we can now override this to be more general in maintainer builds or cvs checkouts */
#ifndef GST_LEVEL_DEFAULT
#define GST_LEVEL_DEFAULT GST_LEVEL_NONE
#endif

/* defines for format (colors etc) - don't change them around, it uses terminal layout 
 * Terminal color strings:
 * 00=none 01=bold 04=underscore 05=blink 07=reverse 08=concealed
 * Text color codes:
 * 30=black 31=red 32=green 33=yellow 34=blue 35=magenta 36=cyan 37=white
 * Background color codes:
 * 40=black 41=red 42=green 43=yellow 44=blue 45=magenta 46=cyan 47=white
 */
typedef enum
{
  /* colors */
  GST_DEBUG_FG_BLACK = 0x0000,
  GST_DEBUG_FG_RED = 0x0001,
  GST_DEBUG_FG_GREEN = 0x0002,
  GST_DEBUG_FG_YELLOW = 0x0003,
  GST_DEBUG_FG_BLUE = 0x0004,
  GST_DEBUG_FG_MAGENTA = 0x0005,
  GST_DEBUG_FG_CYAN = 0x0006,
  GST_DEBUG_FG_WHITE = 0x0007,
  /* background colors */
  GST_DEBUG_BG_BLACK = 0x0000,
  GST_DEBUG_BG_RED = 0x0010,
  GST_DEBUG_BG_GREEN = 0x0020,
  GST_DEBUG_BG_YELLOW = 0x0030,
  GST_DEBUG_BG_BLUE = 0x0040,
  GST_DEBUG_BG_MAGENTA = 0x0050,
  GST_DEBUG_BG_CYAN = 0x0060,
  GST_DEBUG_BG_WHITE = 0x0070,
  /* other formats */
  GST_DEBUG_BOLD = 0x0100,
  GST_DEBUG_UNDERLINE = 0x0200
} GstDebugColorFlags;

#define GST_DEBUG_FG_MASK	(0x000F)
#define GST_DEBUG_BG_MASK	(0x00F0)
#define GST_DEBUG_FORMAT_MASK	(0xFF00)

typedef struct _GstDebugCategory GstDebugCategory;
struct _GstDebugCategory
{
  /*< private > */
  GstAtomicInt *threshold;
  guint color;			/* see defines above */

  const gchar *name;
  const gchar *description;
};

/********** some convenience macros for debugging **********/

/* This is needed in printf's if a char* might be NULL. Solaris crashes then */
#define GST_STR_NULL(str) ((str) ? (str) : "(NULL)")

/* easier debugging for pad names */
#define GST_DEBUG_PAD_NAME(pad) \
  (GST_OBJECT_PARENT(pad) != NULL) ? \
  GST_STR_NULL (GST_OBJECT_NAME (GST_OBJECT_PARENT(pad))) : \
  "''", GST_OBJECT_NAME (pad)

/* You might want to define GST_FUNCTION in apps' configure script */

#ifndef GST_FUNCTION
#if defined (__GNUC__)
#  define GST_FUNCTION     ((const char*) (__PRETTY_FUNCTION__))
#elif defined (G_HAVE_ISO_VARARGS)
#  define GST_FUNCTION     ((const char*) (__func__))
#else
#  define GST_FUNCTION     ((const char*) ("???"))
#endif
#endif /* ifndef GST_FUNCTION */


typedef struct _GstDebugMessage GstDebugMessage;
typedef void (*GstLogFunction) (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer data);

/* Disable this subsystem if no varargs macro can be found. 
   Use a trick so the core builds the functions nonetheless if it wasn't
   explicitly disabled. */
#if !defined(G_HAVE_ISO_VARARGS) && !defined(G_HAVE_GNUC_VARARGS)
#define __GST_DISABLE_GST_DEBUG
#endif
#ifdef GST_DISABLE_GST_DEBUG
#ifndef __GST_DISABLE_GST_DEBUG
#define __GST_DISABLE_GST_DEBUG
#endif
#endif

#ifndef __GST_DISABLE_GST_DEBUG

void _gst_debug_init (void);

/* note we can't use G_GNUC_PRINTF (7, 8) because gcc chokes on %P, which
 * we use for GST_PTR_FORMAT. */
void
gst_debug_log (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line, GObject * object, const gchar * format, ...)
    G_GNUC_NO_INSTRUMENT;
     void gst_debug_log_valist (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line,
    GObject * object, const gchar * format, va_list args) G_GNUC_NO_INSTRUMENT;

     const gchar *gst_debug_message_get (GstDebugMessage * message);

     void gst_debug_log_default (GstDebugCategory * category,
    GstDebugLevel level,
    const gchar * file,
    const gchar * function,
    gint line,
    GObject * object,
    GstDebugMessage * message, gpointer unused) G_GNUC_NO_INSTRUMENT;

     G_CONST_RETURN gchar *gst_debug_level_get_name (GstDebugLevel level);

     void gst_debug_add_log_function (GstLogFunction func, gpointer data);
     guint gst_debug_remove_log_function (GstLogFunction func);
     guint gst_debug_remove_log_function_by_data (gpointer data);

     void gst_debug_set_active (gboolean active);
     gboolean gst_debug_is_active (void);

     void gst_debug_set_colored (gboolean colored);
     gboolean gst_debug_is_colored (void);

     void gst_debug_set_default_threshold (GstDebugLevel level);
     GstDebugLevel gst_debug_get_default_threshold (void);
     void gst_debug_set_threshold_for_name (const gchar * name,
    GstDebugLevel level);
     void gst_debug_unset_threshold_for_name (const gchar * name);

/**
 * GST_DEBUG_CATEGORY:
 * @cat: the category
 *
 * Defines a GstDebugCategory variable.
 * This macro expands to nothing if debugging is disabled.
 */
#define GST_DEBUG_CATEGORY(cat) GstDebugCategory *cat = NULL
/**
 * GST_DEBUG_CATEGORY_EXTERN:
 * @cat: the category
 *
 * Declares a GstDebugCategory variable as extern. Use in header files.
 * This macro expands to nothing if debugging is disabled.
 */
#define GST_DEBUG_CATEGORY_EXTERN(cat) extern GstDebugCategory *cat
/**
 * GST_DEBUG_CATEGORY_STATIC:
 * @cat: the category
 *
 * Defines a static GstDebugCategory variable.
 * This macro expands to nothing if debugging is disabled.
 */
#define GST_DEBUG_CATEGORY_STATIC(cat) static GstDebugCategory *cat = NULL
/* do not use this function, use the macros below */
     GstDebugCategory *_gst_debug_category_new (gchar * name,
    guint color, gchar * description);
/**
 * GST_DEBUG_CATEGORY_INIT:
 * @cat: the category to initialize.
 * @name: the name of the category.
 * @color: the colors to use for a color representation or 0 for no color.
 * @description: optional description of the category.
 *
 * Initializes a new #GstDebugCategory with the given properties and set to
 * the default threshold.
 *
 * <note>
 * <para>
 * This macro expands to nothing if debugging is disabled.
 * </para>
 * <para>
 * When naming your category, please follow the following conventions to ensure
 * that the pattern matching for categories works as expected. It is not
 * earth-shattering if you don't follow these conventions, but it would be nice
 * for everyone.
 * </para>
 * <para>
 * If you define a category for a plugin or a feature of it, name the category
 * like the feature. So if you wanted to write a "filesrc" element, you would
 * name the category "filesrc". Use lowercase letters only.
 * If you define more than one category for the same element, append an
 * underscore and an identifier to your categories, like this: "filesrc_cache"
 * </para>
 * <para>
 * If you create a library or an application using debugging categories, use a
 * common prefix followed by an underscore for all your categories. GStreamer
 * uses the GST prefix so GStreamer categories look like "GST_STATES". Be sure
 * to include uppercase letters.
 * </para>
 * </note>
 */
#define GST_DEBUG_CATEGORY_INIT(cat,name,color,description) G_STMT_START{	\
  if (cat == NULL)								\
    cat = _gst_debug_category_new (name,color,description);			\
}G_STMT_END

     void gst_debug_category_free (GstDebugCategory * category);
     void gst_debug_category_set_threshold (GstDebugCategory * category,
    GstDebugLevel level);
     void gst_debug_category_reset_threshold (GstDebugCategory * category);
     GstDebugLevel gst_debug_category_get_threshold (GstDebugCategory *
    category);
     G_CONST_RETURN gchar *gst_debug_category_get_name (GstDebugCategory *
    category);
     guint gst_debug_category_get_color (GstDebugCategory * category);
     G_CONST_RETURN gchar *gst_debug_category_get_description (GstDebugCategory
    * category);
     GSList *gst_debug_get_all_categories (void);

     gchar *gst_debug_construct_term_color (guint colorinfo);


     extern GstDebugCategory *GST_CAT_DEFAULT;

/* this symbol may not be used */
     extern gboolean __gst_debug_enabled;

#ifdef G_HAVE_ISO_VARARGS
#define GST_CAT_LEVEL_LOG(cat,level,object,...) G_STMT_START{			\
  if (__gst_debug_enabled) {				\
    gst_debug_log ((cat), (level), __FILE__, GST_FUNCTION, __LINE__, (GObject *) (object), __VA_ARGS__); \
  }										\
}G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#define GST_CAT_LEVEL_LOG(cat,level,object,args...) G_STMT_START{			\
  if (__gst_debug_enabled) {				\
    gst_debug_log ((cat), (level), __FILE__, GST_FUNCTION, __LINE__, (GObject *) (object), ##args ); \
  }										\
}G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */

#ifndef GST_DEBUG_ENABLE_DEPRECATED

#ifdef G_HAVE_ISO_VARARGS

#define GST_CAT_ERROR_OBJECT(cat,obj,...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_ERROR,   obj,  __VA_ARGS__)
#define GST_CAT_WARNING_OBJECT(cat,obj,...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_WARNING, obj,  __VA_ARGS__)
#define GST_CAT_INFO_OBJECT(cat,obj,...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_INFO,    obj,  __VA_ARGS__)
#define GST_CAT_DEBUG_OBJECT(cat,obj,...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_DEBUG,   obj,  __VA_ARGS__)
#define GST_CAT_LOG_OBJECT(cat,obj,...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_LOG,     obj,  __VA_ARGS__)

#define GST_CAT_ERROR(cat,...)			GST_CAT_LEVEL_LOG (cat, GST_LEVEL_ERROR,   NULL, __VA_ARGS__)
#define GST_CAT_WARNING(cat,...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_WARNING, NULL, __VA_ARGS__)
#define GST_CAT_INFO(cat,...)			GST_CAT_LEVEL_LOG (cat, GST_LEVEL_INFO,    NULL, __VA_ARGS__)
#define GST_CAT_DEBUG(cat,...)			GST_CAT_LEVEL_LOG (cat, GST_LEVEL_DEBUG,   NULL, __VA_ARGS__)
#define GST_CAT_LOG(cat,...)			GST_CAT_LEVEL_LOG (cat, GST_LEVEL_LOG,     NULL, __VA_ARGS__)

#define GST_ERROR_OBJECT(obj,...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_ERROR,   obj,  __VA_ARGS__)
#define GST_WARNING_OBJECT(obj,...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_WARNING, obj,  __VA_ARGS__)
#define GST_INFO_OBJECT(obj,...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    obj,  __VA_ARGS__)
#define GST_DEBUG_OBJECT(obj,...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   obj,  __VA_ARGS__)
#define GST_LOG_OBJECT(obj,...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_LOG,     obj,  __VA_ARGS__)

#define GST_ERROR(...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_ERROR,   NULL, __VA_ARGS__)
#define GST_WARNING(...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_WARNING, NULL, __VA_ARGS__)
#define GST_INFO(...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    NULL, __VA_ARGS__)
#define GST_DEBUG(...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   NULL, __VA_ARGS__)
#define GST_LOG(...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_LOG,     NULL, __VA_ARGS__)

#else /* G_HAVE_GNUC_VARARGS */

#define GST_CAT_ERROR_OBJECT(cat,obj,args...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_ERROR,   obj,  ##args )
#define GST_CAT_WARNING_OBJECT(cat,obj,args...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_WARNING, obj,  ##args )
#define GST_CAT_INFO_OBJECT(cat,obj,args...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_INFO,    obj,  ##args )
#define GST_CAT_DEBUG_OBJECT(cat,obj,args...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_DEBUG,   obj,  ##args )
#define GST_CAT_LOG_OBJECT(cat,obj,args...)	GST_CAT_LEVEL_LOG (cat, GST_LEVEL_LOG,     obj,  ##args )

#define GST_CAT_ERROR(cat,args...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_ERROR,   NULL, ##args )
#define GST_CAT_WARNING(cat,args...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_WARNING, NULL, ##args )
#define GST_CAT_INFO(cat,args...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_INFO,    NULL, ##args )
#define GST_CAT_DEBUG(cat,args...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_DEBUG,   NULL, ##args )
#define GST_CAT_LOG(cat,args...)		GST_CAT_LEVEL_LOG (cat, GST_LEVEL_LOG,     NULL, ##args )

#define GST_ERROR_OBJECT(obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_ERROR,   obj,  ##args )
#define GST_WARNING_OBJECT(obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_WARNING, obj,  ##args )
#define GST_INFO_OBJECT(obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    obj,  ##args )
#define GST_DEBUG_OBJECT(obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   obj,  ##args )
#define GST_LOG_OBJECT(obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_LOG,     obj,  ##args )

#define GST_ERROR(args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_ERROR,   NULL, ##args )
#define GST_WARNING(args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_WARNING, NULL, ##args )
#define GST_INFO(args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    NULL, ##args )
#define GST_DEBUG(args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   NULL, ##args )
#define GST_LOG(args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_LOG,     NULL, ##args )

#endif /* G_HAVE_ISO_VARARGS */

#else /* GST_DEBUG_ENABLE_DEPRECATED */
/* This is a workaround so the old debugging stuff of GStreamer 0.6 works.
   This is undocumented and will go when 0.8 comes out. */

#ifdef G_HAVE_ISO_VARARGS
#  define GST_INFO(cat,...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    NULL, __VA_ARGS__)
#  define GST_DEBUG(cat,...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   NULL, __VA_ARGS__)
#  define GST_INFO_ELEMENT(cat,obj,...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    obj, __VA_ARGS__)
#  define GST_DEBUG_ELEMENT(cat,obj,...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   obj, __VA_ARGS__)
#  define GST_DEBUG_ENTER(...)			GST_DEBUG ("entering: " __VA_ARGS__ )
#  define GST_DEBUG_LEAVE(...)			GST_DEBUG ("leaving: "  __VA_ARGS__ )
#  define GST_INFO_ENTER(...)			GST_INFO ("entering: " __VA_ARGS__ )
#  define GST_INFO_LEAVE(...)			GST_INFO ("leaving: "  __VA_ARGS__ )
#else /* G_HAVE_GNUC_VARARGS */
#  define GST_INFO(cat,args...)			GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    NULL, ##args )
#  define GST_DEBUG(cat,args...)		GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   NULL, ##args )
#  define GST_INFO_ELEMENT(cat,obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_INFO,    obj, ##args )
#  define GST_DEBUG_ELEMENT(cat,obj,args...)	GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, GST_LEVEL_DEBUG,   obj, ##args )
#  define GST_DEBUG_ENTER(args...)		GST_DEBUG ("entering: " ##args )
#  define GST_DEBUG_LEAVE(args...)		GST_DEBUG ("leaving: "  ##args )
#  define GST_INFO_ENTER(args...)		GST_INFO ("entering: " ##args )
#  define GST_INFO_LEAVE(args...)		GST_INFO ("leaving: "  ##args )
#endif /* G_HAVE_ISO_VARARGS */

#endif /* !GST_DEBUG_ENABLE_DEPRECATED */


/********** function pointer stuff **********/
     void *_gst_debug_register_funcptr (void *ptr, gchar * ptrname);
     G_CONST_RETURN gchar *_gst_debug_nameof_funcptr (void *ptr);

#define GST_DEBUG_FUNCPTR(ptr) (_gst_debug_register_funcptr((void *)(ptr), #ptr) , ptr)
#define GST_DEBUG_FUNCPTR_NAME(ptr) _gst_debug_nameof_funcptr((void *)ptr)

#else /* GST_DISABLE_GST_DEBUG */

#ifdef __GNUC__
#  pragma GCC poison gst_debug_log
#  pragma GCC poison gst_debug_log_valist
#  pragma GCC poison gst_debug_log_default
#  pragma GCC poison _gst_debug_category_new
#endif

#define _gst_debug_init()	/* NOP */

#define gst_debug_set_log_function(func,data)	/* NOP */
#define gst_debug_reset_log_function(void)	/* NOP */
#define gst_debug_set_default_threshold(level)	/* NOP */
#define gst_debug_get_default_threshold()		(GST_LEVEL_NONE)
#define gst_debug_category_set_threshold_for_name(name, level)	/* NOP */
#define gst_debug_category_unset_threshold_for_name(name)	/* NOP */

#define gst_debug_level_get_name(level)			("NONE")
#define gst_debug_add_log_function(func,data)		(FALSE)
#define gst_debug_remove_log_function(func)		(0)
#define gst_debug_remove_log_function_by_data(data)	(0)
#define gst_debug_set_active(active)	/* NOP */
#define gst_debug_is_active()				(FALSE)
#define gst_debug_set_colored(colored)	/* NOP */
#define gst_debug_is_colored()				(FALSE)
#define gst_debug_set_default_threshold(level)	/* NOP */
#define gst_debug_get_default_threshold()		(GST_LEVEL_NONE)
#define gst_debug_set_threshold_for_name(name,level)	/* NOP */
#define gst_debug_unset_threshold_for_name(name)	/* NOP */

#define GST_DEBUG_CATEGORY(var)	/* NOP */
#define GST_DEBUG_CATEGORY_EXTERN(var)	/* NOP */
#define GST_DEBUG_CATEGORY_STATIC(var)	/* NOP */
#define GST_DEBUG_CATEGORY_INIT(var,name,color,desc)	/* NOP */
#define gst_debug_category_free(category)	/* NOP */
#define gst_debug_category_set_threshold(category,level)	/* NOP */
#define gst_debug_category_reset_threshold(category)	/* NOP */
#define gst_debug_category_get_threshold(category)	(GST_LEVEL_NONE)
#define gst_debug_category_get_name(cat)		("")
#define gst_debug_category_get_color(cat)		(0)
#define gst_debug_category_get_description(cat)		("")
#define gst_debug_get_all_categories()			(NULL)
#define gst_debug_construct_term_color(colorinfo)	(g_strdup ("00"))

#ifdef G_HAVE_ISO_VARARGS

#define GST_CAT_LEVEL_LOG(cat,level,...)	/* NOP */

#define GST_CAT_ERROR_OBJECT(...)	/* NOP */
#define GST_CAT_WARNING_OBJECT(...)	/* NOP */
#define GST_CAT_INFO_OBJECT(...)	/* NOP */
#define GST_CAT_DEBUG_OBJECT(...)	/* NOP */
#define GST_CAT_LOG_OBJECT(...)	/* NOP */

#define GST_CAT_ERROR(...)	/* NOP */
#define GST_CAT_WARNING(...)	/* NOP */
#define GST_CAT_INFO(...)	/* NOP */
#define GST_CAT_DEBUG(...)	/* NOP */
#define GST_CAT_LOG(...)	/* NOP */

#define GST_ERROR_OBJECT(...)	/* NOP */
#define GST_WARNING_OBJECT(...)	/* NOP */
#define GST_INFO_OBJECT(...)	/* NOP */
#define GST_DEBUG_OBJECT(...)	/* NOP */
#define GST_LOG_OBJECT(...)	/* NOP */

#define GST_ERROR(...)		/* NOP */
#define GST_WARNING(...)	/* NOP */
#define GST_INFO(...)		/* NOP */
#define GST_DEBUG(...)		/* NOP */
#define GST_LOG(...)		/* NOP */

#ifdef GST_DEBUG_ENABLE_DEPRECATED
#define GST_INFO_ELEMENT(cat,obj,...)	/* NOP */
#define GST_DEBUG_ELEMENT(cat,obj,...)	/* NOP */
#define GST_DEBUG_ENTER(...)	/* NOP */
#define GST_DEBUG_LEAVE(...)	/* NOP */
#define GST_INFO_ENTER(...)	/* NOP */
#define GST_INFO_LEAVE(...)	/* NOP */
#endif /* GST_DEBUG_ENABLE_DEPRECATED */

#else /* !G_HAVE_ISO_VARARGS */

#define GST_CAT_LEVEL_LOG(cat,level,args...)	/* NOP */

#define GST_CAT_ERROR_OBJECT(args...)	/* NOP */
#define GST_CAT_WARNING_OBJECT(args...)	/* NOP */
#define GST_CAT_INFO_OBJECT(args...)	/* NOP */
#define GST_CAT_DEBUG_OBJECT(args...)	/* NOP */
#define GST_CAT_LOG_OBJECT(args...)	/* NOP */

#define GST_CAT_ERROR(args...)	/* NOP */
#define GST_CAT_WARNING(args...)	/* NOP */
#define GST_CAT_INFO(args...)	/* NOP */
#define GST_CAT_DEBUG(args...)	/* NOP */
#define GST_CAT_LOG(args...)	/* NOP */

#define GST_ERROR_OBJECT(args...)	/* NOP */
#define GST_WARNING_OBJECT(args...)	/* NOP */
#define GST_INFO_OBJECT(args...)	/* NOP */
#define GST_DEBUG_OBJECT(args...)	/* NOP */
#define GST_LOG_OBJECT(args...)	/* NOP */

#define GST_ERROR(args...)	/* NOP */
#define GST_WARNING(args...)	/* NOP */
#define GST_INFO(args...)	/* NOP */
#define GST_DEBUG(args...)	/* NOP */
#define GST_LOG(args...)	/* NOP */

#ifdef GST_DEBUG_ENABLE_DEPRECATED
#define GST_INFO_ELEMENT(cat,obj,args...)	/* NOP */
#define GST_DEBUG_ELEMENT(cat,obj,args...)	/* NOP */
#define GST_DEBUG_ENTER(args...)	/* NOP */
#define GST_DEBUG_LEAVE(args...)	/* NOP */
#define GST_INFO_ENTER(args...)	/* NOP */
#define GST_INFO_LEAVE(args...)	/* NOP */
#endif /* GST_DEBUG_ENABLE_DEPRECATED */

#endif /* G_HAVE_ISO_VARARGS */

#define GST_DEBUG_FUNCPTR(ptr) (ptr)
#define GST_DEBUG_FUNCPTR_NAME(ptr) (g_strdup_printf ("%p", ptr))

#endif /* GST_DISABLE_GST_DEBUG */

void gst_debug_print_stack_trace (void);

G_END_DECLS
#endif /* __GSTINFO_H__ */
