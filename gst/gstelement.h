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


#ifndef __GST_ELEMENT_H__
#define __GST_ELEMENT_H__


#include <gnome-xml/parser.h>
#include <gst/gstlog.h>
#include <gst/gstobject.h>
#include <gst/gstpad.h>
#include <gst/gstbuffer.h>
#include <gst/cothreads.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef enum {
  GST_STATE_NONE_PENDING	= -1,
  GST_STATE_NULL		= 0,
  GST_STATE_READY		= 1,
  GST_STATE_PLAYING		= 2,
  GST_STATE_PAUSED		= 3,
} GstElementState;

typedef enum {
  GST_STATE_FAILURE		= 0,
  GST_STATE_SUCCESS		= 1,
  GST_STATE_ASYNC		= 2,
} GstElementStateReturn;

static inline char *_gst_print_statename(int state) {
  switch (state) {
    case -1: return "none pending";break;
    case 0: return "null";break;
    case 1: return "ready";break;
    case 2: return "playing";break;
    case 3: return "paused";break;
    default: return "";
  }
  return "";
}

#define GST_STATE(obj)			(GST_ELEMENT(obj)->current_state)
#define GST_STATE_PENDING(obj)		(GST_ELEMENT(obj)->pending_state)

#define GST_TYPE_ELEMENT \
  (gst_element_get_type())
#define GST_ELEMENT(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_ELEMENT,GstElement))
#define GST_ELEMENT_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENT,GstElementClass))
#define GST_IS_ELEMENT(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_ELEMENT))
#define GST_IS_ELEMENT_CLASS(klass) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENT))

typedef enum {
  GST_ELEMENT_MULTI_IN		= (1 << 4),
  GST_ELEMENT_THREAD_SUGGESTED	= (1 << 5),
  GST_ELEMENT_NO_SEEK		= (1 << 6),
} GstElementFlags;

#define GST_ELEMENT_IS_MULTI_IN(obj)	(GST_FLAGS(obj) & GST_ELEMENT_MULTI_IN)
#define GST_ELEMENT_IS_THREAD_SUGGESTED(obj)	(GST_FLAGS(obj) & GST_ELEMENT_THREAD_SUGGESTED)


typedef struct _GstElement GstElement;
typedef struct _GstElementClass GstElementClass;
typedef struct _GstElementDetails GstElementDetails;
typedef struct _GstElementFactory GstElementFactory;

typedef void (*GstElementLoopFunction) (GstElement *element);

struct _GstElement {
  GstObject object;

  gchar *name;

  guint8 current_state;
  guint8 pending_state;

  GstElementLoopFunction loopfunc;
  cothread_state *threadstate;

  guint16 numpads;
  GList *pads;

  GstElement *manager;
};

struct _GstElementClass {
  GstObjectClass parent_class;

  /* the elementfactory that created us */
  GstElementFactory *elementfactory;

  /* signal callbacks */
  void (*state_change) 	(GstElement *element,GstElementState state);
  void (*new_pad) 	(GstElement *element,GstPad *pad);
  void (*new_ghost_pad) (GstElement *element,GstPad *pad);
  void (*error) 	(GstElement *element,gchar *error);

  /* events */
//  gboolean (*start) (GstElement *element,GstElementState state);
//  gboolean (*stop) (GstElement *element);

  /* change the element state */
  GstElementStateReturn (*change_state) (GstElement *element);

  /* create or read XML representation of self */
  xmlNodePtr 		(*save_thyself) (GstElement *element, xmlNodePtr parent);
  void 			(*restore_thyself) (GstElement *element, xmlNodePtr self, GHashTable *elements);
};

struct _GstElementDetails {
  gchar *longname;              /* long, english name */
  gchar *class;                 /* type of element, kinda */
  gchar *description;           /* insights of one form or another */
  gchar *version;               /* version of the element */
  gchar *author;                /* who wrote this thing? */
  gchar *copyright;             /* copyright details (year, etc.) */
};

struct _GstElementFactory {
  gchar *name;			/* name of element */
  GtkType type;			/* unique GtkType of element */

  GstElementDetails *details;	/* pointer to details struct */

  GList *src_types;
  GList *sink_types;
};

GtkType 		gst_element_get_type		(void);
GstElement*		gst_element_new			(void);
#define 		gst_element_destroy(element) 	gst_object_destroy (GST_OBJECT (element))

void 			gst_element_set_loop_function	(GstElement *element,
                                   			 GstElementLoopFunction loop);

void 			gst_element_set_name		(GstElement *element, gchar *name);
const gchar*		gst_element_get_name		(GstElement *element);

void 			gst_element_set_manager		(GstElement *element, GstElement *manager);
GstElement*		gst_element_get_manager		(GstElement *element);

void 			gst_element_add_pad		(GstElement *element, GstPad *pad);
GstPad*			gst_element_get_pad		(GstElement *element, gchar *name);
GList*			gst_element_get_pad_list	(GstElement *element);
void 			gst_element_add_ghost_pad	(GstElement *element, GstPad *pad);

void 			gst_element_connect		(GstElement *src, gchar *srcpadname,
                         				 GstElement *dest, gchar *destpadname);

/* called by the app to set the state of the element */
gint 			gst_element_set_state		(GstElement *element, GstElementState state);

void 			gst_element_error		(GstElement *element, gchar *error);

GstElementFactory*	gst_element_get_factory		(GstElement *element);
int 			gst_element_loopfunc_wrapper	(int argc,char **argv);

/* XML write and read */
xmlNodePtr 		gst_element_save_thyself	(GstElement *element, xmlNodePtr parent);
GstElement*		gst_element_load_thyself	(xmlNodePtr parent, GHashTable *elements);


GstElementFactory*	gst_elementfactory_new		(gchar *name,GtkType type,
                                          		 GstElementDetails *details);
void 			gst_elementfactory_register	(GstElementFactory *elementfactory);

void 			gst_elementfactory_add_src	(GstElementFactory *elementfactory, guint16 id);
void 			gst_elementfactory_add_sink	(GstElementFactory *elementfactory, guint16 id);

GstElementFactory*	gst_elementfactory_find		(gchar *name);
GList*			gst_elementfactory_get_list	(void);

GstElement*		gst_elementfactory_create	(GstElementFactory *factory,
                                      			 gchar *name);
// FIXME this name is wrong, probably so is the one above it
GstElement*		gst_elementfactory_make		(gchar *factoryname, gchar *name);

xmlNodePtr 		gst_elementfactory_save_thyself	(GstElementFactory *factory, xmlNodePtr parent); 
GstElementFactory*	gst_elementfactory_load_thyself	(xmlNodePtr parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ELEMENT_H__ */     

