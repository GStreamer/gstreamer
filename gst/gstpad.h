/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpad.h: Header for GstPad object
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


#ifndef __GST_PAD_H__
#define __GST_PAD_H__

#include <parser.h> // NOTE: This is xml-config's fault

// Include compatability defines: if libxml hasn't already defined these,
// we have an old version 1.x
#ifndef xmlChildrenNode
#define xmlChildrenNode childs
#define xmlRootNode root
#endif

#include <gst/gstobject.h>
#include <gst/gstbuffer.h>
#include <gst/cothreads.h>
#include <gst/gstcaps.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_PAD                 	(gst_pad_get_type ())
#define GST_PAD(obj)                 	(GTK_CHECK_CAST ((obj), GST_TYPE_PAD,GstPad))
#define GST_PAD_CLASS(klass)         	(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD,GstPadClass))
#define GST_IS_PAD(obj)              	(GTK_CHECK_TYPE ((obj), GST_TYPE_PAD))
#define GST_IS_PAD_CLASS(obj)        	(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD))

/* quick test to see if the pad is connected */
#define GST_PAD_CONNECTED(pad) 		((pad) && (pad)->peer != NULL)
#define GST_PAD_CAN_PULL(pad) 		((pad) && (pad)->pullfunc != NULL)


typedef struct _GstPad GstPad;
typedef struct _GstPadClass GstPadClass;
typedef struct _GstPadTemplate GstPadTemplate;
typedef struct _GstPadTemplateClass GstPadTemplateClass;

/* this defines the functions used to chain buffers
 * pad is the sink pad (so the same chain function can be used for N pads)
 * buf is the buffer being passed */
typedef void (*GstPadChainFunction) (GstPad *pad,GstBuffer *buf);
typedef GstBuffer *(*GstPadGetFunction) (GstPad *pad);
typedef GstBuffer *(*GstPadGetRegionFunction) (GstPad *pad, gulong offset, gulong size);
typedef void (*GstPadQoSFunction) (GstPad *pad, glong qos_message);

typedef void (*GstPadPushFunction) (GstPad *pad, GstBuffer *buf);
typedef GstBuffer *(*GstPadPullFunction) (GstPad *pad);
typedef GstBuffer *(*GstPadPullRegionFunction) (GstPad *pad, gulong offset, gulong size);

typedef gboolean (*GstPadEOSFunction) (GstPad *pad);

typedef enum {
  GST_PAD_UNKNOWN,
  GST_PAD_SRC,
  GST_PAD_SINK,
} GstPadDirection;

typedef enum {
  GST_PAD_DISABLED		= GST_OBJECT_FLAG_LAST,
  GST_PAD_EOS,

  GST_PAD_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 4,
} GstPadFlags;

struct _GstPad {
  GstObject object;

  gchar *name;
  GList *caps;
  gpointer element_private;

  cothread_state *threadstate;

  GstPadDirection direction;

  GstPad *peer;

  GstBuffer *bufpen;

  GstPadChainFunction chainfunc;
  GstPadGetFunction getfunc;
  GstPadGetRegionFunction getregionfunc;
  GstPadQoSFunction qosfunc;
  GstPadEOSFunction eosfunc;

  GstPadPushFunction pushfunc;
  GstPadPullFunction pullfunc;
  GstPadPullRegionFunction pullregionfunc;

  GstObject *parent;
  GList *ghostparents;

  GstPadTemplate *padtemplate;	/* the template for this pad */
};

struct _GstPadClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*set_active)	(GstPad *pad, gboolean active);
  void (*caps_changed)	(GstPad *pad, GstCaps *newcaps);
  void (*eos)		(GstPad *pad);
};

/* template */
#define GST_TYPE_PADTEMPLATE           	(gst_padtemplate_get_type ())
#define GST_PADTEMPLATE(obj)           	(GTK_CHECK_CAST ((obj), GST_TYPE_PADTEMPLATE,GstPad))
#define GST_PADTEMPLATE_CLASS(klass)   	(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_PADTEMPLATE,GstPadClass))
#define GST_IS_PADTEMPLATE(obj)        	(GTK_CHECK_TYPE ((obj), GST_TYPE_PADTEMPLATE))
#define GST_IS_PADTEMPLATE_CLASS(obj)  	(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_PADTEMPLATE))

typedef enum {
  GST_PAD_ALWAYS,
  GST_PAD_SOMETIMES,
} GstPadPresence;

struct _GstPadTemplate {
  GstObject 	  object;

  gchar           *name_template;
  GstPadDirection direction;
  GstPadPresence  presence;
  GList  	  *caps;
};

struct _GstPadTemplateClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*pad_created)	(GstPadTemplate *temp, GstPad *pad);
};


/* factory */
typedef gpointer GstPadFactoryEntry;
typedef GstPadFactoryEntry GstPadFactory[];

#define GST_PAD_FACTORY_ALWAYS 		GINT_TO_POINTER(GST_PAD_ALWAYS)
#define GST_PAD_FACTORY_SOMETIMES 	GINT_TO_POINTER(GST_PAD_SOMETIMES)

#define GST_PAD_FACTORY_SRC	 	GINT_TO_POINTER(GST_PAD_SRC)
#define GST_PAD_FACTORY_SINK 		GINT_TO_POINTER(GST_PAD_SINK)

#define GST_PAD_FACTORY_CAPS(a...) 	GINT_TO_POINTER(1),##a,NULL

GtkType 		gst_pad_get_type		(void);

GstPad*			gst_pad_new			(gchar *name, GstPadDirection direction);
#define 		gst_pad_destroy(pad) 		gst_object_destroy (GST_OBJECT (pad))
GstPad*			gst_pad_new_from_template	(GstPadTemplate *temp, gchar *name);

GstPadDirection 	gst_pad_get_direction		(GstPad *pad);

void 			gst_pad_set_chain_function	(GstPad *pad, GstPadChainFunction chain);
void 			gst_pad_set_get_function	(GstPad *pad, GstPadGetFunction get);
void			gst_pad_set_getregion_function	(GstPad *pad, GstPadGetRegionFunction getregion);
void 			gst_pad_set_qos_function	(GstPad *pad, GstPadQoSFunction qos);
void			gst_pad_set_eos_function	(GstPad *pad, GstPadEOSFunction eos);

void	 		gst_pad_set_caps_list		(GstPad *pad, GList *caps);
GList* 			gst_pad_get_caps_list		(GstPad *pad);
GstCaps* 		gst_pad_get_caps_by_name	(GstPad *pad, gchar *name);
gboolean 		gst_pad_check_compatibility	(GstPad *srcpad, GstPad *sinkpad);

void 			gst_pad_set_name		(GstPad *pad, const gchar *name);
const gchar*		gst_pad_get_name		(GstPad *pad);

void			gst_pad_set_element_private	(GstPad *pad, gpointer priv);
gpointer		gst_pad_get_element_private	(GstPad *pad);

void 			gst_pad_set_parent		(GstPad *pad, GstObject *parent);
GstObject*		gst_pad_get_parent		(GstPad *pad);
void 			gst_pad_add_ghost_parent	(GstPad *pad, GstObject *parent);
void 			gst_pad_remove_ghost_parent	(GstPad *pad, GstObject *parent);
GList*			gst_pad_get_ghost_parents	(GstPad *pad);

GstPad*			gst_pad_get_peer		(GstPad *pad);

void 			gst_pad_connect			(GstPad *srcpad, GstPad *sinkpad);
void 			gst_pad_disconnect		(GstPad *srcpad, GstPad *sinkpad);

#if 1
void 			gst_pad_push			(GstPad *pad, GstBuffer *buffer);
#else
#define gst_pad_push(pad,buf) G_STMT_START{ \
  if ((pad)->peer->pushfunc) ((pad)->peer->pushfunc)((pad)->peer,(buf)); \
}G_STMT_END
#endif
#if 1
GstBuffer*		gst_pad_pull			(GstPad *pad);
GstBuffer*		gst_pad_pull_region		(GstPad *pad, gulong offset, gulong size);
#else
#define gst_pad_pull(pad) \
  (((pad)->peer->pullfunc) ? ((pad)->peer->pullfunc)((pad)->peer) : NULL)
#define gst_pad_pullregion(pad,offset,size) \
  (((pad)->peer->pullregionfunc) ? ((pad)->peer->pullregionfunc)((pad)->peer,(offset),(size)) : NULL)
#endif

GstPad *		gst_pad_select			(GstPad *nextpad, ...);

#define			gst_pad_eos(pad)		((pad)->peer->eosfunc((pad)->peer))
gboolean		gst_pad_set_eos			(GstPad *pad);

gboolean		gst_pad_eos_func		(GstPad *pad);
void 			gst_pad_handle_qos		(GstPad *pad, glong qos_message);

xmlNodePtr 		gst_pad_save_thyself		(GstPad *pad, xmlNodePtr parent);
void 			gst_pad_load_and_connect	(xmlNodePtr parent, GstObject *element, GHashTable *elements);


/* templates and factories */
GtkType 		gst_padtemplate_get_type	(void);

GstPadTemplate*		gst_padtemplate_new		(GstPadFactory *factory);
GstPadTemplate*		gst_padtemplate_create		(gchar *name_template, 
		                                         GstPadDirection direction, GstPadPresence presence,
							 GList *caps);

xmlNodePtr 		gst_padtemplate_save_thyself	(GstPadTemplate *pad, xmlNodePtr parent);
GstPadTemplate*		gst_padtemplate_load_thyself	(xmlNodePtr parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PAD_H__ */     

