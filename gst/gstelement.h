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
#include <gst/gstpluginfeature.h>

G_BEGIN_DECLS

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

#define GST_ELEMENT_CAST(obj)		((GstElement*)(obj))
#define GST_ELEMENT_CLASS_CAST(klass)	((GstElementClass*)(klass))
#define GST_IS_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ELEMENT))

#ifdef GST_TYPE_PARANOID
# define GST_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ELEMENT, GstElement))
# define GST_ELEMENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ELEMENT, GstElementClass))
#else
# define GST_ELEMENT                    GST_ELEMENT_CAST
# define GST_ELEMENT_CLASS              GST_ELEMENT_CLASS_CAST
#endif

typedef enum {
  /* element is complex (for some def.) and generally require a cothread */
  GST_ELEMENT_COMPLEX		= GST_OBJECT_FLAG_LAST,
  /* input and output pads aren't directly coupled to each other
     examples: queues, multi-output async readers, etc. */
  GST_ELEMENT_DECOUPLED,
  /* this element should be placed in a thread if at all possible */
  GST_ELEMENT_THREAD_SUGGESTED,
  /* this element is incable of seeking (FIXME: does this apply to filters?) */
  GST_ELEMENT_NO_SEEK,
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

  /* use some padding for future expansion */
  GST_ELEMENT_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 16,
} GstElementFlags;

#define GST_ELEMENT_IS_THREAD_SUGGESTED(obj)	(GST_FLAG_IS_SET(obj,GST_ELEMENT_THREAD_SUGGESTED))
#define GST_ELEMENT_IS_EOS(obj)			(GST_FLAG_IS_SET(obj,GST_ELEMENT_EOS))
#define GST_ELEMENT_IS_EVENT_AWARE(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_EVENT_AWARE))
#define GST_ELEMENT_IS_DECOUPLED(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_DECOUPLED))

#define GST_ELEMENT_NAME(obj)			(GST_OBJECT_NAME(obj))
#define GST_ELEMENT_PARENT(obj)			(GST_OBJECT_PARENT(obj))
#define GST_ELEMENT_MANAGER(obj)		(((GstElement*)(obj))->manager)
#define GST_ELEMENT_SCHED(obj)			(((GstElement*)(obj))->sched)
#define GST_ELEMENT_CLOCK(obj)			(((GstElement*)(obj))->clock)
#define GST_ELEMENT_PADS(obj)			((obj)->pads)

typedef struct _GstElementFactory GstElementFactory;
typedef struct _GstElementFactoryClass GstElementFactoryClass;

typedef void 		(*GstElementLoopFunction) 	(GstElement *element);
typedef void 		(*GstElementSetClockFunction) 	(GstElement *element, GstClock *clock);
typedef GstClock* 	(*GstElementGetClockFunction) 	(GstElement *element);
typedef void 		(*GstElementPreRunFunction) 	(GstElement *element);
typedef void 		(*GstElementPostRunFunction) 	(GstElement *element);

struct _GstElement {
  GstObject 		object;

  /* element state  and scheduling */
  guint8 		current_state;
  guint8 		pending_state;
  GstElement 		*manager;
  GstElementLoopFunction loopfunc;

  GstScheduler 		*sched;
  gpointer		sched_private;
  GstElementSetClockFunction setclockfunc;
  GstElementGetClockFunction getclockfunc;

  /* element pads */
  guint16 		numpads;
  guint16 		numsrcpads;
  guint16 		numsinkpads;
  GList 		*pads;
  GstPad 		*select_pad;

  GMutex 		*state_mutex;
  GCond 		*state_cond;

  GstElementPreRunFunction  pre_run_func;
  GstElementPostRunFunction post_run_func;
  GAsyncQueue		*prop_value_queue;
  GMutex		*property_mutex;
};

struct _GstElementClass {
  GstObjectClass 	parent_class;

  /* the elementfactory that created us */
  GstElementFactory 	*elementfactory;
  /* templates for our pads */
  GList 		*padtemplates;
  gint 			numpadtemplates;
  
  /* signal callbacks */
  void (*state_change)	(GstElement *element, GstElementState old, GstElementState state);
  void (*new_pad)	(GstElement *element, GstPad *pad);
  void (*pad_removed)	(GstElement *element, GstPad *pad);
  void (*error)		(GstElement *element, GstElement *source, gchar *error);
  void (*eos)		(GstElement *element);
  void (*deep_notify)	(GstObject *object, GstObject *orig, GParamSpec *pspec);

  /* local pointers for get/set */
  void (*set_property) 	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
  void (*get_property)	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

  /* vtable*/
  /* change the element state */
  GstElementStateReturn (*change_state)		(GstElement *element);
  /* request a new pad */
  GstPad*		(*request_new_pad)	(GstElement *element, GstPadTemplate *templ, const gchar* name);
  void			(*release_pad)		(GstElement *element, GstPad *pad);
};

void			gst_element_class_add_pad_template	(GstElementClass *klass, GstPadTemplate *templ);
void                    gst_element_class_install_std_props	(GstElementClass *klass,
								 const gchar      *first_name, ...);


GType			gst_element_get_type		(void);
#define			gst_element_destroy(element)	gst_object_destroy (GST_OBJECT (element))

void			gst_element_set_loop_function	(GstElement *element,
							 GstElementLoopFunction loop);

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

void                    gst_element_set_name            (GstElement *element, const gchar *name);
const gchar*            gst_element_get_name            (GstElement *element);

void                    gst_element_set_parent          (GstElement *element, GstObject *parent);
GstObject*              gst_element_get_parent          (GstElement *element);

GstClock*		gst_element_get_clock 		(GstElement *element);
void			gst_element_set_clock 		(GstElement *element, GstClock *clock);
GstClockReturn		gst_element_clock_wait 		(GstElement *element, GstClock *clock, GstClockTime time);

void			gst_element_yield		(GstElement *element);
gboolean		gst_element_interrupt		(GstElement *element);
void			gst_element_set_scheduler	(GstElement *element, GstScheduler *sched);
GstScheduler*		gst_element_get_scheduler	(GstElement *element);

void			gst_element_add_pad		(GstElement *element, GstPad *pad);
void			gst_element_remove_pad		(GstElement *element, GstPad *pad);
GstPad *		gst_element_add_ghost_pad	(GstElement *element, GstPad *pad, gchar *name);
void			gst_element_remove_ghost_pad	(GstElement *element, GstPad *pad);

GstPad*			gst_element_get_pad		(GstElement *element, const gchar *name);
GstPad*			gst_element_get_static_pad	(GstElement *element, const gchar *name);
GstPad*			gst_element_get_request_pad	(GstElement *element, const gchar *name);
void			gst_element_release_request_pad	(GstElement *element, GstPad *pad);

GList*			gst_element_get_pad_list	(GstElement *element);
GstPad*			gst_element_get_compatible_pad	(GstElement *element, GstPad *pad);
GstPad*			gst_element_get_compatible_pad_filtered (GstElement *element, GstPad *pad, 
							 GstCaps *filtercaps);
/* unimplemented
GstPad*			gst_element_get_compatible_request_pad (GstElement *element, GstPadTemplate *templ);
GstPad*			gst_element_get_compatible_static_pad (GstElement *element, GstPadTemplate *templ);
*/

GstPadTemplate*		gst_element_get_pad_template		(GstElement *element, const guchar *name);
GList*			gst_element_get_pad_template_list	(GstElement *element);
GstPadTemplate*		gst_element_get_compatible_pad_template (GstElement *element, GstPadTemplate *compattempl);

gboolean		gst_element_connect		(GstElement *src, GstElement *dest);
gboolean		gst_element_connect_many 	(GstElement *element_1, GstElement *element_2, ...);
gboolean		gst_element_connect_filtered 	(GstElement *src, GstElement *dest,
							 GstCaps *filtercaps);
void			gst_element_disconnect 		(GstElement *src, GstElement *dest);
void			gst_element_disconnect_many 	(GstElement *element_1, GstElement *element_2, ...);

gboolean		gst_element_connect_pads	(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);
gboolean		gst_element_connect_pads_filtered (GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname,
							 GstCaps *filtercaps);
void			gst_element_disconnect_pads	(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);

void			gst_element_set_eos		(GstElement *element);

void 			gst_element_error 		(GstElement *element, const gchar *error, ...);

GstElementState         gst_element_get_state           (GstElement *element);
gint			gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_wait_state_change 	(GstElement *element);
	
const gchar*		gst_element_state_get_name	(GstElementState state);

GstElementFactory*	gst_element_get_factory		(GstElement *element);

GstBin*			gst_element_get_managing_bin	(GstElement *element);


/*
 *
 * factories stuff
 *
 **/
typedef struct _GstElementDetails GstElementDetails;

struct _GstElementDetails {
  gchar *longname;              /* long, english name */
  gchar *klass;                 /* type of element, as hierarchy */
  gchar *description;           /* insights of one form or another */
  gchar *version;               /* version of the element */
  gchar *author;                /* who wrote this thing? */
  gchar *copyright;             /* copyright details (year, etc.) */
};

#define GST_TYPE_ELEMENT_FACTORY 		(gst_element_factory_get_type())
#define GST_ELEMENT_FACTORY(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactory))
#define GST_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactoryClass))
#define GST_IS_ELEMENT_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ELEMENT_FACTORY))
#define GST_IS_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENT_FACTORY))

struct _GstElementFactory {
  GstPluginFeature feature;

  GType type;			/* unique GType of element */

  guint details_dynamic : 1;

  GstElementDetails *details;	/* pointer to details struct */

  GList *padtemplates;
  guint16 numpadtemplates;
};

struct _GstElementFactoryClass {
  GstPluginFeatureClass parent_class;
};

GType 			gst_element_factory_get_type 		(void);

GstElementFactory*	gst_element_factory_new			(const gchar *name, GType type,
                                                                 GstElementDetails *details);
GstElementFactory*	gst_element_factory_find		(const gchar *name);

void			gst_element_factory_add_pad_template	(GstElementFactory *elementfactory,
								 GstPadTemplate *templ);

gboolean		gst_element_factory_can_src_caps	(GstElementFactory *factory,
								 GstCaps *caps);
gboolean		gst_element_factory_can_sink_caps	(GstElementFactory *factory,
								 GstCaps *caps);

GstElement*		gst_element_factory_create		(GstElementFactory *factory,
								 const gchar *name);
/* FIXME this name is wrong, probably so is the one above it */
GstElement*		gst_element_factory_make		(const gchar *factoryname, const gchar *name);

G_END_DECLS


#endif /* __GST_ELEMENT_H__ */

