/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

#include <gst/gstobject.h>
#include <gst/gstbuffer.h>
#include <gst/cothreads.h>
#include <gst/gstcaps.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_PAD			(gst_pad_get_type ())
#define GST_PAD(obj)			(GTK_CHECK_CAST ((obj), GST_TYPE_PAD,GstPad))
#define GST_PAD_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD,GstPadClass))
#define GST_IS_PAD(obj)			(GTK_CHECK_TYPE ((obj), GST_TYPE_PAD))
#define GST_IS_PAD_CLASS(obj)		(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD))

#define GST_TYPE_REAL_PAD		(gst_real_pad_get_type ())
#define GST_REAL_PAD(obj)		(GTK_CHECK_CAST ((obj), GST_TYPE_REAL_PAD,GstRealPad))
#define GST_REAL_PAD_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_REAL_PAD,GstRealPadClass))
#define GST_IS_REAL_PAD(obj)		(GTK_CHECK_TYPE ((obj), GST_TYPE_REAL_PAD))
#define GST_IS_REAL_PAD_CLASS(obj)	(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_REAL_PAD))

#define GST_TYPE_GHOST_PAD		(gst_ghost_pad_get_type ())
#define GST_GHOST_PAD(obj)		(GTK_CHECK_CAST ((obj), GST_TYPE_GHOST_PAD,GstGhostPad))
#define GST_GHOST_PAD_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_GHOST_PAD,GstGhostPadClass))
#define GST_IS_GHOST_PAD(obj)		(GTK_CHECK_TYPE ((obj), GST_TYPE_GHOST_PAD))
#define GST_IS_GHOST_PAD_CLASS(obj)	(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_GHOST_PAD))


typedef struct _GstPad GstPad;
typedef struct _GstPadClass GstPadClass;
typedef struct _GstRealPad GstRealPad;
typedef struct _GstRealPadClass GstRealPadClass;
typedef struct _GstGhostPad GstGhostPad;
typedef struct _GstGhostPadClass GstGhostPadClass;
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
  gpointer element_private;
  GstObject *parent;

  GstPadTemplate *padtemplate;	/* the template for this pad */
};

struct _GstPadClass {
  GstObjectClass parent_class;
};

struct _GstRealPad {
  GstPad pad;

  GList *caps;
  GstPadDirection direction;

  cothread_state *threadstate;

  GstRealPad *peer;

  GstBuffer *bufpen;

  GstPadChainFunction chainfunc;
  GstPadGetFunction getfunc;
  GstPadGetRegionFunction getregionfunc;
  GstPadQoSFunction qosfunc;
  GstPadEOSFunction eosfunc;

  GstPadPushFunction pushfunc;
  GstPadPullFunction pullfunc;
  GstPadPullRegionFunction pullregionfunc;

  GList *ghostpads;
};

struct _GstRealPadClass {
  GstPadClass parent_class;

  /* signal callbacks */
  void (*set_active)	(GstPad *pad, gboolean active);
  void (*caps_changed)	(GstPad *pad, GstCaps *newcaps);
  void (*eos)		(GstPad *pad);
};

struct _GstGhostPad {
  GstPad pad;

  GstRealPad *realpad;
};

struct _GstGhostPadClass {
  GstPadClass parent_class;
};


/***** helper macros *****/
/* GstPad */
#define GST_PAD_NAME(pad)		(((GstPad *)(pad))->name)
#define GST_PAD_ELEMENT_PRIVATE(pad)	(((GstPad *)(pad))->element_private)
#define GST_PAD_PARENT(pad)		(((GstPad *)(pad))->parent)
#define GST_PAD_PADTEMPLATE(pad)	(((GstPad *)(pad))->padtemplate)

/* GstRealPad */
#define GST_RPAD_DIRECTION(pad)		(((GstRealPad *)(pad))->direction)
#define GST_RPAD_CAPS(pad)		(((GstRealPad *)(pad))->caps)
#define GST_RPAD_PEER(pad)		(((GstRealPad *)(pad))->peer)
#define GST_RPAD_BUFPEN(pad)		(((GstRealPad *)(pad))->bufpen)
#define GST_RPAD_CHAINFUNC(pad)		(((GstRealPad *)(pad))->chainfunc)
#define GST_RPAD_GETFUNC(pad)		(((GstRealPad *)(pad))->getfunc)
#define GST_RPAD_GETREGIONFUNC(pad)	(((GstRealPad *)(pad))->getregionfunc)
#define GST_RPAD_PUSHFUNC(pad)		(((GstRealPad *)(pad))->pushfunc)
#define GST_RPAD_PULLFUNC(pad)		(((GstRealPad *)(pad))->pullfunc)
#define GST_RPAD_PULLREGIONFUNC(pad)	(((GstRealPad *)(pad))->pullregionfunc)
#define GST_RPAD_QOSFUNC(pad)		(((GstRealPad *)(pad))->qosfunc)
#define GST_RPAD_EOSFUNC(pad)		(((GstRealPad *)(pad))->eosfunc)

/* GstGhostPad */
#define GST_GPAD_REALPAD(pad)		(((GstGhostPad *)(pad))->realpad)

/* Generic */
#define GST_PAD_REALIZE(pad) (GST_IS_REAL_PAD(pad) ? ((GstRealPad *)(pad)) : GST_GPAD_REALPAD(pad))
#define GST_PAD_DIRECTION(pad)		GST_RPAD_DIRECTION(GST_PAD_REALIZE(pad))
#define GST_PAD_CAPS(pad)		GST_RPAD_CAPS(GST_PAD_REALIZE(pad))
#define GST_PAD_PEER(pad)		GST_RPAD_PEER(GST_PAD_REALIZE(pad))

/* Some check functions (unused?) */
#define GST_PAD_CONNECTED(pad) 		(GST_IS_REAL_PAD(pad) && GST_REAL_PAD(pad)->peer != NULL)
#define GST_PAD_CAN_PULL(pad) 		(GST_IS_REAL_PAD(pad) && GST_REAL_PAD(pad)->pullfunc != NULL)


/***** PadTemplate *****/
#define GST_TYPE_PADTEMPLATE           	(gst_padtemplate_get_type ())
#define GST_PADTEMPLATE(obj)           	(GTK_CHECK_CAST ((obj), GST_TYPE_PADTEMPLATE,GstPad))
#define GST_PADTEMPLATE_CLASS(klass)   	(GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_PADTEMPLATE,GstPadClass))
#define GST_IS_PADTEMPLATE(obj)        	(GTK_CHECK_TYPE ((obj), GST_TYPE_PADTEMPLATE))
#define GST_IS_PADTEMPLATE_CLASS(obj)  	(GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_PADTEMPLATE))

typedef enum {
  GST_PAD_ALWAYS,
  GST_PAD_SOMETIMES,
  GST_PAD_REQUEST,
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
  void (*pad_created)	(GstPadTemplate *templ, GstPad *pad);
};


/* factory */
typedef gpointer GstPadFactoryEntry;
typedef GstPadFactoryEntry GstPadFactory[];

#define GST_PAD_FACTORY_ALWAYS 		GINT_TO_POINTER(GST_PAD_ALWAYS)
#define GST_PAD_FACTORY_SOMETIMES 	GINT_TO_POINTER(GST_PAD_SOMETIMES)
#define GST_PAD_FACTORY_REQUEST 	GINT_TO_POINTER(GST_PAD_REQUEST)

#define GST_PAD_FACTORY_SRC	 	GINT_TO_POINTER(GST_PAD_SRC)
#define GST_PAD_FACTORY_SINK 		GINT_TO_POINTER(GST_PAD_SINK)

#define GST_PAD_FACTORY_CAPS(a...) 	GINT_TO_POINTER(1),##a,NULL


GtkType 		gst_pad_get_type		(void);
GtkType 		gst_real_pad_get_type		(void);
GtkType 		gst_ghost_pad_get_type		(void);

GstPad*			gst_pad_new			(gchar *name, GstPadDirection direction);
#define 		gst_pad_destroy(pad) 		gst_object_destroy (GST_OBJECT (pad))
GstPad*			gst_pad_new_from_template	(GstPadTemplate *templ, gchar *name);

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
void 			gst_pad_add_ghost_pad		(GstPad *pad, GstPad *ghostpad);
void 			gst_pad_remove_ghost_pad	(GstPad *pad, GstPad *ghostpad);
GList*			gst_pad_get_ghost_pad_list	(GstPad *pad);

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

#define			gst_pad_eos(pad)	(GST_RPAD_EOSFUNC(GST_RPAD_PEER(pad))(GST_PAD(GST_RPAD_PEER(pad))))
gboolean		gst_pad_set_eos			(GstPad *pad);

gboolean		gst_pad_eos_func		(GstPad *pad);
void 			gst_pad_handle_qos		(GstPad *pad, glong qos_message);

xmlNodePtr 		gst_pad_save_thyself		(GstPad *pad, xmlNodePtr parent);
void 			gst_pad_load_and_connect	(xmlNodePtr parent, GstObject *element, GHashTable *elements);


/* ghostpads */
GstPad *		gst_ghost_pad_new		(gchar *name,GstPad *pad);



/* templates and factories */
GtkType 		gst_padtemplate_get_type	(void);

GstPadTemplate*		gst_padtemplate_new		(GstPadFactory *factory);
GstPadTemplate*		gst_padtemplate_create		(gchar *name_template, 
		                                         GstPadDirection direction, GstPadPresence presence,
							 GList *caps);

xmlNodePtr 		gst_padtemplate_save_thyself	(GstPadTemplate *templ, xmlNodePtr parent);
GstPadTemplate*		gst_padtemplate_load_thyself	(xmlNodePtr parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PAD_H__ */     

