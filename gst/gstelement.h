/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelement.h: Header for GstElement
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


#ifndef __GST_ELEMENT_H__
#define __GST_ELEMENT_H__

#include <gst/gstconfig.h>
#include <gst/gsttypes.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstclock.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstindex.h>
#include <gst/gsttag.h>

G_BEGIN_DECLS

typedef struct _GstElementDetails GstElementDetails;

/* FIXME: need translatable stuff in here (how handle in registry)? */
struct _GstElementDetails {
  gchar *longname;              /* long, english name */
  gchar *klass;                 /* type of element, as hierarchy */
  gchar *description;           /* insights of one form or another */
  gchar *author;                /* who wrote this thing? */

  gpointer _gst_reserved[GST_PADDING];
};
#define GST_ELEMENT_DETAILS(longname,klass,description,author)		\
  { longname, klass, description, author, GST_PADDING_INIT }
#define GST_IS_ELEMENT_DETAILS(details) (					\
  (details) && ((details)->longname != NULL) && ((details)->klass != NULL)	\
  && ((details)->description != NULL) && ((details)->author != NULL))

#define GST_NUM_STATES 4

/* NOTE: this probably should be done with an #ifdef to decide 
 * whether to safe-cast or to just do the non-checking cast.
 */
#define GST_STATE(obj)			(GST_ELEMENT(obj)->current_state)
#define GST_STATE_PENDING(obj)		(GST_ELEMENT(obj)->pending_state)

/* Note: using 8 bit shift mostly "just because", it leaves us enough room to grow <g> */
#define GST_STATE_TRANSITION(obj)	((GST_STATE(obj)<<8) | GST_STATE_PENDING(obj))
#define GST_STATE_NULL_TO_READY		((GST_STATE_NULL<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_PAUSED	((GST_STATE_READY<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_PLAYING	((GST_STATE_PAUSED<<8) | GST_STATE_PLAYING)
#define GST_STATE_PLAYING_TO_PAUSED	((GST_STATE_PLAYING<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_READY	((GST_STATE_PAUSED<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_NULL		((GST_STATE_READY<<8) | GST_STATE_NULL)

extern GType _gst_element_type;

#define GST_TYPE_ELEMENT		(_gst_element_type)
#define GST_IS_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ELEMENT))
#define GST_ELEMENT_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ELEMENT, GstElementClass))
#define GST_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ELEMENT, GstElement))
#define GST_ELEMENT_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ELEMENT, GstElementClass))

/* convenience functions */
#ifdef G_HAVE_ISO_VARARGS
#define GST_ELEMENT_QUERY_TYPE_FUNCTION(functionname, ...) \
	GST_QUERY_TYPE_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#define GST_ELEMENT_FORMATS_FUNCTION(functionname, ...)    \
	GST_FORMATS_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#define GST_ELEMENT_EVENT_MASK_FUNCTION(functionname, ...) \
	GST_EVENT_MASK_FUNCTION (GstElement*, functionname, __VA_ARGS__);
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_ELEMENT_QUERY_TYPE_FUNCTION(functionname, a...) \
	GST_QUERY_TYPE_FUNCTION (GstElement*, functionname, a);
#define GST_ELEMENT_FORMATS_FUNCTION(functionname, a...)    \
	GST_FORMATS_FUNCTION (GstElement*, functionname, a);
#define GST_ELEMENT_EVENT_MASK_FUNCTION(functionname, a...) \
	GST_EVENT_MASK_FUNCTION (GstElement*, functionname, a);
#endif

typedef enum {
  /* element is complex (for some def.) and generally require a cothread */
  GST_ELEMENT_COMPLEX		= GST_OBJECT_FLAG_LAST,
  /* input and output pads aren't directly coupled to each other
     examples: queues, multi-output async readers, etc. */
  GST_ELEMENT_DECOUPLED,
  /* this element should be placed in a thread if at all possible */
  GST_ELEMENT_THREAD_SUGGESTED,
  /* this element, for some reason, has a loop function that performs
   * an infinite loop without calls to gst_element_yield () */
  GST_ELEMENT_INFINITE_LOOP,
  /* there is a new loopfunction ready for placement */
  GST_ELEMENT_NEW_LOOPFUNC,
  /* if this element can handle events */
  GST_ELEMENT_EVENT_AWARE,
  /* use threadsafe property get/set implementation */
  GST_ELEMENT_USE_THREADSAFE_PROPERTIES,

  /* private flags that can be used by the scheduler */
  GST_ELEMENT_SCHEDULER_PRIVATE1,
  GST_ELEMENT_SCHEDULER_PRIVATE2,

  /* ignore state changes from parent */
  GST_ELEMENT_LOCKED_STATE,

  /* element is in error */
  GST_ELEMENT_ERROR,

  /* use some padding for future expansion */
  GST_ELEMENT_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 16
} GstElementFlags;

#define GST_ELEMENT_IS_THREAD_SUGGESTED(obj)	(GST_FLAG_IS_SET(obj,GST_ELEMENT_THREAD_SUGGESTED))
#define GST_ELEMENT_IS_EVENT_AWARE(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_EVENT_AWARE))
#define GST_ELEMENT_IS_DECOUPLED(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_DECOUPLED))

#define GST_ELEMENT_NAME(obj)			(GST_OBJECT_NAME(obj))
#define GST_ELEMENT_PARENT(obj)			(GST_OBJECT_PARENT(obj))
#define GST_ELEMENT_MANAGER(obj)		(((GstElement*)(obj))->manager)
#define GST_ELEMENT_SCHED(obj)			(((GstElement*)(obj))->sched)
#define GST_ELEMENT_CLOCK(obj)			(((GstElement*)(obj))->clock)
#define GST_ELEMENT_PADS(obj)			((obj)->pads)

#define gst_element_error(el, domain, code, message, debug) \
  gst_element_error_extended (GST_ELEMENT(el), \
  GST_ ## domain ## _ERROR, GST_ ## domain ## _ERROR_ ## code, \
  g_strdup_printf message, \
  g_strdup_printf debug, \
  __FILE__, GST_FUNCTION, __LINE__)

typedef struct _GstElementFactory GstElementFactory;
typedef struct _GstElementFactoryClass GstElementFactoryClass;

typedef void 		(*GstElementLoopFunction) 	(GstElement *element);
typedef void 		(*GstElementPreRunFunction) 	(GstElement *element);
typedef void 		(*GstElementPostRunFunction) 	(GstElement *element);

struct _GstElement {
  GstObject 		object;

  /* element state  and scheduling */
  guint8 		current_state;
  guint8 		pending_state;
  GstElementLoopFunction loopfunc;

  GstScheduler 		*sched;
  gpointer		sched_private;

  /* allocated clock */
  GstClock		*clock;
  GstClockTimeDiff    	base_time; /* NULL/READY: 0 - PAUSED: current time - PLAYING: difference to clock */

  /* element pads */
  guint16 		numpads;
  guint16 		numsrcpads;
  guint16 		numsinkpads;
  GList 		*pads;

  GMutex 		*state_mutex;
  GCond 		*state_cond;

  GstElementPreRunFunction  pre_run_func;
  GstElementPostRunFunction post_run_func;
  GAsyncQueue		*prop_value_queue;
  GMutex		*property_mutex;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstElementClass {
  GstObjectClass 	parent_class;

  /* the element details */
  GstElementDetails 	details;

  /* factory that the element was created from */
  GstElementFactory	*elementfactory;

  /* templates for our pads */
  GList 		*padtemplates;
  gint 			numpadtemplates;
  
  /* signal callbacks */
  void (*state_change)	(GstElement *element, GstElementState old, GstElementState state);
  void (*new_pad)	(GstElement *element, GstPad *pad);
  void (*pad_removed)	(GstElement *element, GstPad *pad);
  void (*error)		(GstElement *element, GstElement *source, GError *error, gchar *debug);
  void (*eos)		(GstElement *element);
  void (*found_tag)	(GstElement *element, GstElement *source, GstTagList *tag_list);

  /* local pointers for get/set */
  void (*set_property) 	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
  void (*get_property)	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

  /* vtable*/
  gboolean		(*release_locks)	(GstElement *element);

  /* query/convert/events functions */
  const GstEventMask*   (*get_event_masks)     	(GstElement *element);
  gboolean		(*send_event)		(GstElement *element, GstEvent *event);
  const GstFormat*      (*get_formats)        	(GstElement *element);
  gboolean              (*convert)        	(GstElement *element,
		                                 GstFormat  src_format,  gint64  src_value,
						 GstFormat *dest_format, gint64 *dest_value);
  const GstQueryType* 	(*get_query_types)    	(GstElement *element);
  gboolean		(*query)		(GstElement *element, GstQueryType type,
		  				 GstFormat *format, gint64 *value);

  /* change the element state */
  GstElementStateReturn (*change_state)		(GstElement *element);

  /* request/release pads */
  GstPad*		(*request_new_pad)	(GstElement *element, GstPadTemplate *templ, const gchar* name);
  void			(*release_pad)		(GstElement *element, GstPad *pad);

  /* set/get clocks */
  GstClock*		(*get_clock)		(GstElement *element);
  void			(*set_clock)		(GstElement *element, GstClock *clock);

  /* index */
  GstIndex*		(*get_index)		(GstElement *element);
  void			(*set_index)		(GstElement *element, GstIndex *index);

  GstElementStateReturn	(*set_state)		(GstElement *element, GstElementState state);
  gpointer _gst_reserved[GST_PADDING - 1];
};

void			gst_element_class_add_pad_template	(GstElementClass *klass, GstPadTemplate *templ);
void                    gst_element_class_install_std_props	(GstElementClass *klass,
								 const gchar      *first_name, ...);
void			gst_element_class_set_details		(GstElementClass *klass,
								 const GstElementDetails *details);

#define 		gst_element_default_deep_notify 	gst_object_default_deep_notify

void 			gst_element_default_error		(GObject *object, GstObject *orig, GError *error, gchar *debug);

GType			gst_element_get_type		(void);
void			gst_element_set_loop_function	(GstElement *element,
							 GstElementLoopFunction loop);

#define			gst_element_get_name(elem)	gst_object_get_name(GST_OBJECT(elem))
#define			gst_element_set_name(elem,name)	gst_object_set_name(GST_OBJECT(elem),name)
#define			gst_element_get_parent(elem)	gst_object_get_parent(GST_OBJECT(elem))
#define			gst_element_set_parent(elem,parent)	gst_object_set_parent(GST_OBJECT(elem),parent)

/* threadsafe versions of their g_object_* counterparts */
void	    		gst_element_set			(GstElement *element, const gchar *first_property_name, ...);
void        		gst_element_get			(GstElement *element, const gchar *first_property_name, ...);
void			gst_element_set_valist		(GstElement *element, const gchar *first_property_name,
                                                         va_list var_args);
void			gst_element_get_valist		(GstElement *element, const gchar *first_property_name,
                                                         va_list var_args);
void			gst_element_set_property	(GstElement *element, const gchar *property_name,
                                                         const GValue   *value);
void			gst_element_get_property	(GstElement *element, const gchar *property_name,
                                                         GValue *value);

void			gst_element_enable_threadsafe_properties	(GstElement *element);
void			gst_element_disable_threadsafe_properties	(GstElement *element);
void			gst_element_set_pending_properties		(GstElement *element);

/* clocking */
gboolean		gst_element_requires_clock	(GstElement *element);
gboolean		gst_element_provides_clock	(GstElement *element);
GstClock*		gst_element_get_clock 		(GstElement *element);
void			gst_element_set_clock 		(GstElement *element, GstClock *clock);
#ifndef GST_DEISABLE_DEPRECATED
GstClockReturn		gst_element_clock_wait 		(GstElement *element, 
							 GstClockID id, GstClockTimeDiff *jitter);
#endif
GstClockTime		gst_element_get_time		(GstElement *element);
gboolean		gst_element_wait		(GstElement *element, GstClockTime timestamp);
void			gst_element_set_time		(GstElement *element, GstClockTime time);
void			gst_element_adjust_time		(GstElement *element, GstClockTimeDiff diff);
 
/* indexs */
gboolean		gst_element_is_indexable	(GstElement *element);
void			gst_element_set_index		(GstElement *element, GstIndex *index);
GstIndex*		gst_element_get_index		(GstElement *element);


gboolean		gst_element_release_locks	(GstElement *element);

void			gst_element_yield		(GstElement *element);
gboolean		gst_element_interrupt		(GstElement *element);
void			gst_element_set_scheduler	(GstElement *element, GstScheduler *sched);
GstScheduler*		gst_element_get_scheduler	(GstElement *element);

void			gst_element_add_pad		(GstElement *element, GstPad *pad);
void			gst_element_remove_pad		(GstElement *element, GstPad *pad);
GstPad *		gst_element_add_ghost_pad	(GstElement *element, GstPad *pad, const gchar *name);
void			gst_element_remove_ghost_pad	(GstElement *element, GstPad *pad);

GstPad*			gst_element_get_pad		(GstElement *element, const gchar *name);
GstPad*			gst_element_get_static_pad	(GstElement *element, const gchar *name);
GstPad*			gst_element_get_request_pad	(GstElement *element, const gchar *name);
void			gst_element_release_request_pad	(GstElement *element, GstPad *pad);

G_CONST_RETURN GList*
			gst_element_get_pad_list	(GstElement *element);
GstPad*			gst_element_get_compatible_pad	(GstElement *element, GstPad *pad);
GstPad*			gst_element_get_compatible_pad_filtered (GstElement *element, GstPad *pad, 
							 const GstCaps *filtercaps);

GstPadTemplate*		gst_element_class_get_pad_template	(GstElementClass *element_class, const gchar *name);
GList*                  gst_element_class_get_pad_template_list (GstElementClass *element_class);
GstPadTemplate*		gst_element_get_pad_template		(GstElement *element, const gchar *name);
GList*			gst_element_get_pad_template_list	(GstElement *element);
GstPadTemplate*		gst_element_get_compatible_pad_template (GstElement *element, GstPadTemplate *compattempl);

gboolean		gst_element_link		(GstElement *src, GstElement *dest);
gboolean		gst_element_link_many 		(GstElement *element_1, 
							 GstElement *element_2, ...);
gboolean		gst_element_link_filtered 	(GstElement *src, GstElement *dest,
							 const GstCaps *filtercaps);
void			gst_element_unlink 		(GstElement *src, GstElement *dest);
void			gst_element_unlink_many 	(GstElement *element_1, 
							 GstElement *element_2, ...);

gboolean		gst_element_link_pads		(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);
gboolean		gst_element_link_pads_filtered 	(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname,
							 const GstCaps *filtercaps);
void			gst_element_unlink_pads		(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);

G_CONST_RETURN GstEventMask*
			gst_element_get_event_masks	(GstElement *element);
gboolean		gst_element_send_event		(GstElement *element, GstEvent *event);
gboolean		gst_element_seek		(GstElement *element, GstSeekType seek_type,
							 guint64 offset);
G_CONST_RETURN GstQueryType*
			gst_element_get_query_types	(GstElement *element);
gboolean		gst_element_query		(GstElement *element, GstQueryType type,
			                                 GstFormat *format, gint64 *value);
G_CONST_RETURN GstFormat*
			gst_element_get_formats		(GstElement *element);
gboolean		gst_element_convert		(GstElement *element, 
		 					 GstFormat  src_format,  gint64  src_value,
							 GstFormat *dest_format, gint64 *dest_value);

void			gst_element_found_tags		(GstElement *element, GstTagList *tag_list);
void			gst_element_found_tags_for_pad	(GstElement *element, GstPad *pad, GstClockTime timestamp, 
							 GstTagList *list);

void			gst_element_set_eos		(GstElement *element);

void			gst_element_error_extended	(GstElement *element, GQuark domain, gint code, gchar *message, gchar *debug, const gchar *file, const gchar *function, gint line);

gboolean		gst_element_is_locked_state	(GstElement *element);
void			gst_element_set_locked_state	(GstElement *element, gboolean locked_state);
gboolean		gst_element_sync_state_with_parent (GstElement *element);

GstElementState         gst_element_get_state           (GstElement *element);
GstElementStateReturn	gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_wait_state_change 	(GstElement *element);
	
G_CONST_RETURN gchar*	gst_element_state_get_name	(GstElementState state);

GstElementFactory*	gst_element_get_factory		(GstElement *element);

GstBin*			gst_element_get_managing_bin	(GstElement *element);


/*
 *
 * factories stuff
 *
 **/

#define GST_TYPE_ELEMENT_FACTORY 		(gst_element_factory_get_type())
#define GST_ELEMENT_FACTORY(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactory))
#define GST_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactoryClass))
#define GST_IS_ELEMENT_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ELEMENT_FACTORY))
#define GST_IS_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENT_FACTORY))

struct _GstElementFactory {
  GstPluginFeature	parent;

  GType			type;			/* unique GType of element or 0 if not loaded */

  GstElementDetails	details;

  GList *		padtemplates;
  guint			numpadtemplates;

  /* URI interface stuff */
  guint			uri_type;
  gchar **		uri_protocols;
  
  GList *		interfaces;		/* interfaces this element implements */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstElementFactoryClass {
  GstPluginFeatureClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

GType 			gst_element_factory_get_type 		(void);

gboolean		gst_element_register			(GstPlugin *plugin,
								 const gchar *elementname,
								 guint rank,
								 GType type);

GstElementFactory *	gst_element_factory_find		(const gchar *name);
GType			gst_element_factory_get_element_type	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_longname	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_klass		(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_description  	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_version      	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_author		(GstElementFactory *factory);
guint			gst_element_factory_get_num_pad_templates (GstElementFactory *factory);
G_CONST_RETURN GList *	gst_element_factory_get_pad_templates	(GstElementFactory *factory);
guint			gst_element_factory_get_uri_type	(GstElementFactory *factory);		
gchar **		gst_element_factory_get_uri_protocols	(GstElementFactory *factory);		

GstElement*		gst_element_factory_create		(GstElementFactory *factory,
								 const gchar *name);
GstElement*		gst_element_factory_make		(const gchar *factoryname, const gchar *name);

gboolean		gst_element_factory_can_src_caps	(GstElementFactory *factory,
								 const GstCaps *caps);
gboolean		gst_element_factory_can_sink_caps	(GstElementFactory *factory,
								 const GstCaps *caps);

void			__gst_element_factory_add_pad_template	(GstElementFactory *elementfactory,
								 GstPadTemplate *templ);
void			__gst_element_factory_add_interface	(GstElementFactory *elementfactory,
								 const gchar *interfacename);


G_END_DECLS


#endif /* __GST_ELEMENT_H__ */

