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

#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/cothreads.h>
#include <gst/gstpluginfeature.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_NUM_STATES 4

typedef enum {
  GST_STATE_VOID_PENDING	= 0,
  GST_STATE_NULL		= (1 << 0),
  GST_STATE_READY		= (1 << 1),
  GST_STATE_PAUSED		= (1 << 2),
  GST_STATE_PLAYING		= (1 << 3),
} GstElementState;

typedef enum {
  GST_STATE_FAILURE		= 0,
  GST_STATE_SUCCESS		= 1,
  GST_STATE_ASYNC		= 2,
} GstElementStateReturn;


// NOTE: this probably should be done with an #ifdef to decide whether to safe-cast
// or to just do the non-checking cast.
#define GST_STATE(obj)			(GST_ELEMENT(obj)->current_state)
#define GST_STATE_PENDING(obj)		(GST_ELEMENT(obj)->pending_state)

// Note: using 8 bit shift mostly "just because", it leaves us enough room to grow <g>
#define GST_STATE_TRANSITION(obj)	((GST_STATE(obj)<<8) | GST_STATE_PENDING(obj))
#define GST_STATE_NULL_TO_READY		((GST_STATE_NULL<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_PAUSED	((GST_STATE_READY<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_PLAYING	((GST_STATE_PAUSED<<8) | GST_STATE_PLAYING)
#define GST_STATE_PLAYING_TO_PAUSED	((GST_STATE_PLAYING<<8) | GST_STATE_PAUSED)
#define GST_STATE_PAUSED_TO_READY	((GST_STATE_PAUSED<<8) | GST_STATE_READY)
#define GST_STATE_READY_TO_NULL		((GST_STATE_READY<<8) | GST_STATE_NULL)

extern GType _gst_element_type;

#define GST_TYPE_ELEMENT		(_gst_element_type)

#define GST_ELEMENT_FAST(obj)		((GstElement*)(obj))
#define GST_ELEMENT_CLASS_FAST(klass)	((GstElementClass*)(klass))
#define GST_IS_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_ELEMENT))

#ifdef GST_TYPE_PARANOID
# define GST_ELEMENT(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ELEMENT, GstElement))
# define GST_ELEMENT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_ELEMENT, GstElementClass))
#else
# define GST_ELEMENT                    GST_ELEMENT_FAST
# define GST_ELEMENT_CLASS              GST_ELEMENT_CLASS_FAST
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

  /***** !!!!! need to have a flag that says that an element must
    *not* be an entry into a scheduling chain !!!!! *****/
  /* this element for some reason doesn't obey COTHREAD_STOPPING, or
     has some other reason why it can't be the entry */
  GST_ELEMENT_NO_ENTRY,

  /* there is a new loopfunction ready for placement */
  GST_ELEMENT_NEW_LOOPFUNC,
  /* the cothread holding this element needs to be stopped */
  GST_ELEMENT_COTHREAD_STOPPING,
  /* the element has to be scheduled as a cothread for any sanity */
  GST_ELEMENT_USE_COTHREAD,

  /* if this element can handle events */
  GST_ELEMENT_EVENT_AWARE,

  /* use some padding for future expansion */
  GST_ELEMENT_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 12,
} GstElementFlags;

#define GST_ELEMENT_IS_THREAD_SUGGESTED(obj)	(GST_FLAG_IS_SET(obj,GST_ELEMENT_THREAD_SUGGESTED))
#define GST_ELEMENT_IS_COTHREAD_STOPPING(obj)	(GST_FLAG_IS_SET(obj,GST_ELEMENT_COTHREAD_STOPPING))
#define GST_ELEMENT_IS_EOS(obj)			(GST_FLAG_IS_SET(obj,GST_ELEMENT_EOS))
#define GST_ELEMENT_IS_EVENT_AWARE(obj)		(GST_FLAG_IS_SET(obj,GST_ELEMENT_EVENT_AWARE))

#define GST_ELEMENT_NAME(obj)			(GST_OBJECT_NAME(obj))
#define GST_ELEMENT_PARENT(obj)			(GST_OBJECT_PARENT(obj))
#define GST_ELEMENT_MANAGER(obj)		(((GstElement*)(obj))->manager)
#define GST_ELEMENT_SCHED(obj)			(((GstElement*)(obj))->sched)
#define GST_ELEMENT_PADS(obj)			((obj)->pads)

//typedef struct _GstElement GstElement;
//typedef struct _GstElementClass GstElementClass;
typedef struct _GstElementFactory GstElementFactory;
typedef struct _GstElementFactoryClass GstElementFactoryClass;

typedef void (*GstElementLoopFunction) (GstElement *element);

struct _GstElement {
  GstObject 		object;

  /* element state  and scheduling */
  guint8 		current_state;
  guint8 		pending_state;
  GstElement 		*manager;
  GstScheduler 		*sched;
  GstElementLoopFunction loopfunc;
  cothread_state 	*threadstate;

  /* element pads */
  guint16 		numpads;
  guint16 		numsrcpads;
  guint16 		numsinkpads;
  GList 		*pads;
  GstPad 		*select_pad;
};

struct _GstElementClass {
  GstObjectClass 	parent_class;

  /* the elementfactory that created us */
  GstElementFactory 	*elementfactory;
  /* templates for our pads */
  GList 		*padtemplates;
  gint 			numpadtemplates;
  
  /* signal callbacks */
  void (*state_change)		(GstElement *element, GstElementState old, GstElementState state);
  void (*new_pad)		(GstElement *element, GstPad *pad);
  void (*pad_removed)		(GstElement *element, GstPad *pad);
  void (*error)			(GstElement *element, gchar *error);
  void (*eos)			(GstElement *element);

  /* local pointers for get/set */
  void (*set_property) 	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
  void (*get_property)	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);

  /* change the element state */
  GstElementStateReturn (*change_state)		(GstElement *element);
  /* request a new pad */
  GstPad*		(*request_new_pad)	(GstElement *element, GstPadTemplate *templ, const gchar* name);
};

void			gst_element_class_add_padtemplate	(GstElementClass *klass, GstPadTemplate *templ);

GType			gst_element_get_type		(void);
#define			gst_element_destroy(element)	gst_object_destroy (GST_OBJECT (element))

void			gst_element_set_loop_function	(GstElement *element,
							 GstElementLoopFunction loop);

void                    gst_element_set_name            (GstElement *element, const gchar *name);
const gchar*            gst_element_get_name            (GstElement *element);

void                    gst_element_set_parent          (GstElement *element, GstObject *parent);
GstObject*              gst_element_get_parent          (GstElement *element);

void			gst_element_set_sched		(GstElement *element, GstScheduler *sched);
GstScheduler*		gst_element_get_sched		(GstElement *element);

void			gst_element_add_pad		(GstElement *element, GstPad *pad);
void			gst_element_remove_pad		(GstElement *element, GstPad *pad);
GstPad*			gst_element_get_pad		(GstElement *element, const gchar *name);
GList*			gst_element_get_pad_list	(GstElement *element);
GList*			gst_element_get_padtemplate_list	(GstElement *element);
GstPadTemplate*		gst_element_get_padtemplate_by_name	(GstElement *element, const guchar *name);
void			gst_element_add_ghost_pad	(GstElement *element, GstPad *pad, gchar *name);
void			gst_element_remove_ghost_pad	(GstElement *element, GstPad *pad);

GstPad*			gst_element_request_compatible_pad (GstElement *element, GstPadTemplate *templ);
GstPad*			gst_element_request_pad_by_name	(GstElement *element, const gchar *name);

void			gst_element_connect		(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);
void			gst_element_disconnect		(GstElement *src, const gchar *srcpadname,
							 GstElement *dest, const gchar *destpadname);

void			gst_element_signal_eos		(GstElement *element);


GstElementState         gst_element_get_state           (GstElement *element);
gint			gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_wait_state_change 	(GstElement *element);
	
const gchar*		gst_element_statename		(GstElementState state);

void			gst_element_error		(GstElement *element, const gchar *error);

GstElementFactory*	gst_element_get_factory		(GstElement *element);

#ifndef GST_DISABLE_LOADSAVE
/* XML write and read */
GstElement*		gst_element_restore_thyself	(xmlNodePtr self, GstObject *parent);
#endif


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

#define GST_TYPE_ELEMENTFACTORY 		(gst_elementfactory_get_type())
#define GST_ELEMENTFACTORY(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ELEMENTFACTORY,\
						 GstElementFactory))
#define GST_ELEMENTFACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENTFACTORY,\
						 GstElementFactoryClass))
#define GST_IS_ELEMENTFACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ELEMENTFACTORY))
#define GST_IS_ELEMENTFACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENTFACTORY))

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

GType 			gst_elementfactory_get_type 		(void);

GstElementFactory*	gst_elementfactory_new			(const gchar *name,GType type,
                                                                 GstElementDetails *details);

GstElementFactory*	gst_elementfactory_find			(const gchar *name);
const GList*		gst_elementfactory_get_list		(void);

void			gst_elementfactory_add_padtemplate	(GstElementFactory *elementfactory,
								 GstPadTemplate *templ);

gboolean		gst_elementfactory_can_src_caps		(GstElementFactory *factory,
								 GstCaps *caps);
gboolean		gst_elementfactory_can_sink_caps	(GstElementFactory *factory,
								 GstCaps *caps);

GstElement*		gst_elementfactory_create		(GstElementFactory *factory,
								 const gchar *name);
/* FIXME this name is wrong, probably so is the one above it */
GstElement*		gst_elementfactory_make			(const gchar *factoryname, const gchar *name);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ELEMENT_H__ */

