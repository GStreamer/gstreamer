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

#include <gst/gstconfig.h>

#include <gst/gstobject.h>
#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstevent.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GType _gst_pad_type;
extern GType _gst_real_pad_type;
extern GType _gst_ghost_pad_type;

#define GST_TYPE_PARANOID 

/* 
 * Pad base class
 */
#define GST_TYPE_PAD			(_gst_pad_type)

#define GST_PAD_CAST(obj)		((GstPad*)(obj))
#define GST_PAD_CLASS_CAST(klass)	((GstPadClass*)(klass))
#define GST_IS_PAD(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PAD))
#define GST_IS_PAD_FAST(obj)		(G_OBJECT_TYPE(obj) == GST_TYPE_REAL_PAD || \
					 G_OBJECT_TYPE(obj) == GST_TYPE_GHOST_PAD)
#define GST_IS_PAD_CLASS(obj)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_PAD(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PAD, GstPad))
# define GST_PAD_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD, GstPadClass))
#else
# define GST_PAD			GST_PAD_CAST
# define GST_PAD_CLASS			GST_PAD_CLASS_CAST
#endif

/* 
 * Real Pads
 */
#define GST_TYPE_REAL_PAD		(_gst_real_pad_type)

#define GST_REAL_PAD_CAST(obj)		((GstRealPad*)(obj))
#define GST_REAL_PAD_CLASS_CAST(klass)	((GstRealPadClass*)(klass))
#define GST_IS_REAL_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_REAL_PAD))
#define GST_IS_REAL_PAD_FAST(obj)	(G_OBJECT_TYPE(obj) == GST_TYPE_REAL_PAD)
#define GST_IS_REAL_PAD_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_REAL_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_REAL_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_REAL_PAD, GstRealPad))
# define GST_REAL_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_REAL_PAD, GstRealPadClass))
#else
# define GST_REAL_PAD			GST_REAL_PAD_CAST
# define GST_REAL_PAD_CLASS		GST_REAL_PAD_CLASS_CAST
#endif

/* 
 * Ghost Pads
 */
#define GST_TYPE_GHOST_PAD		(_gst_ghost_pad_type)

#define GST_GHOST_PAD_CAST(obj)		((GstGhostPad*)(obj))
#define GST_GHOST_PAD_CLASS_CAST(klass)	((GstGhostPadClass*)(klass))
#define GST_IS_GHOST_PAD(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GHOST_PAD))
#define GST_IS_GHOST_PAD_FAST(obj)	(G_OBJECT_TYPE(obj) == GST_TYPE_GHOST_PAD)
#define GST_IS_GHOST_PAD_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GHOST_PAD))

#ifdef GST_TYPE_PARANOID
# define GST_GHOST_PAD(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GHOST_PAD, GstGhostPad))
# define GST_GHOST_PAD_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GHOST_PAD, GstGhostPadClass))
#else
# define GST_GHOST_PAD			GST_GHOST_PAD_CAST
# define GST_GHOST_PAD_CLASS		GST_GHOST_PAD_CLASS_CAST
#endif


/*typedef struct _GstPad GstPad; */
/*typedef struct _GstPadClass GstPadClass;*/
typedef struct _GstRealPad GstRealPad;
typedef struct _GstRealPadClass GstRealPadClass;
typedef struct _GstGhostPad GstGhostPad;
typedef struct _GstGhostPadClass GstGhostPadClass;
/*typedef struct _GstPadTemplate GstPadTemplate;*/
/*typedef struct _GstPadTemplateClass GstPadTemplateClass;*/


typedef enum {
  GST_REGION_VOID,
  GST_REGION_OFFSET_LEN,
  GST_REGION_TIME_LEN,
} GstRegionType;

typedef enum {
  GST_PAD_CONNECT_REFUSED = -1,
  GST_PAD_CONNECT_DELAYED =  0,
  GST_PAD_CONNECT_OK      =  1,
  GST_PAD_CONNECT_DONE    =  2,
} GstPadConnectReturn;

/* this defines the functions used to chain buffers
 * pad is the sink pad (so the same chain function can be used for N pads)
 * buf is the buffer being passed */
typedef void 			(*GstPadChainFunction) 		(GstPad *pad,GstBuffer *buf);
typedef GstBuffer*		(*GstPadGetFunction) 		(GstPad *pad);
typedef gboolean		(*GstPadEventFunction)		(GstPad *pad, GstEvent *event);

typedef GstBuffer*		(*GstPadGetRegionFunction) 	(GstPad *pad, GstRegionType type, 
								 guint64 offset, guint64 len);
typedef GstBuffer*		(*GstPadPullRegionFunction) 	(GstPad *pad, GstRegionType type, 
								 guint64 offset, guint64 len);
typedef GstPadConnectReturn	(*GstPadConnectFunction) 	(GstPad *pad, GstCaps *caps);
typedef GstCaps*		(*GstPadGetCapsFunction) 	(GstPad *pad, GstCaps *caps);
typedef GstBufferPool*		(*GstPadBufferPoolFunction) 	(GstPad *pad);

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
  GstObject 		object;

  gpointer 		element_private;

  GstPadTemplate 	*padtemplate;	/* the template for this pad */
};

struct _GstPadClass {
  GstObjectClass parent_class;
};

struct _GstRealPad {
  GstPad 			pad;

  GstCaps 			*caps;
  GstCaps 			*filter;
  GstCaps 			*appfilter;
  GstPadDirection 		direction;

  GstScheduler			*sched;
  gpointer			sched_private;

  GstRealPad 			*peer;

  GstBuffer 			*bufpen;
  GstRegionType 		regiontype;
  guint64 			offset;
  guint64 			len;

  GstPadChainFunction 		chainfunc;
  GstPadChainFunction 		chainhandler;
  GstPadGetFunction 		getfunc;
  GstPadGetFunction		gethandler;

  GstPadEventFunction		eventfunc;
  GstPadEventFunction		eventhandler;

  GstPadGetRegionFunction 	getregionfunc;
  GstPadPullRegionFunction 	pullregionfunc;

  GstPadGetCapsFunction 	getcapsfunc;
  GstPadConnectFunction 	connectfunc;
  GstPadBufferPoolFunction 	bufferpoolfunc;

  GList *ghostpads;
};

struct _GstRealPadClass {
  GstPadClass parent_class;

  /* signal callbacks */
  void (*set_active)		(GstPad *pad, gboolean active);
  void (*caps_changed)		(GstPad *pad, GstCaps *newcaps);
  void (*caps_nego_failed)	(GstPad *pad);
  void (*connected)		(GstPad *pad, GstPad *peer);
  void (*disconnected)		(GstPad *pad, GstPad *peer);
  void (*event_received)	(GstPad *pad, GstEvent *event);

  void (*eos)			(GstPad *pad);
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
#define GST_PAD_NAME(pad)		(GST_OBJECT_NAME(pad))
#define GST_PAD_PARENT(pad)		((GstElement *)(GST_OBJECT_PARENT(pad)))
#define GST_PAD_ELEMENT_PRIVATE(pad)	(((GstPad *)(pad))->element_private)
#define GST_PAD_PADTEMPLATE(pad)	(((GstPad *)(pad))->padtemplate)

/* GstRealPad */
#define GST_RPAD_DIRECTION(pad)		(((GstRealPad *)(pad))->direction)
#define GST_RPAD_CAPS(pad)		(((GstRealPad *)(pad))->caps)
#define GST_RPAD_FILTER(pad)		(((GstRealPad *)(pad))->filter)
#define GST_RPAD_APPFILTER(pad)		(((GstRealPad *)(pad))->appfilter)
#define GST_RPAD_PEER(pad)		(((GstRealPad *)(pad))->peer)
#define GST_RPAD_BUFPEN(pad)		(((GstRealPad *)(pad))->bufpen)
#define GST_RPAD_SCHED(pad)		(((GstRealPad *)(pad))->sched)
#define GST_RPAD_CHAINFUNC(pad)		(((GstRealPad *)(pad))->chainfunc)
#define GST_RPAD_CHAINHANDLER(pad)	(((GstRealPad *)(pad))->chainhandler)
#define GST_RPAD_GETFUNC(pad)		(((GstRealPad *)(pad))->getfunc)
#define GST_RPAD_GETHANDLER(pad)	(((GstRealPad *)(pad))->gethandler)
#define GST_RPAD_EVENTFUNC(pad)		(((GstRealPad *)(pad))->eventfunc)
#define GST_RPAD_EVENTHANDLER(pad)	(((GstRealPad *)(pad))->eventhandler)

#define GST_RPAD_GETREGIONFUNC(pad)	(((GstRealPad *)(pad))->getregionfunc)
#define GST_RPAD_PULLREGIONFUNC(pad)	(((GstRealPad *)(pad))->pullregionfunc)

#define GST_RPAD_CONNECTFUNC(pad)	(((GstRealPad *)(pad))->connectfunc)
#define GST_RPAD_GETCAPSFUNC(pad)	(((GstRealPad *)(pad))->getcapsfunc)
#define GST_RPAD_BUFFERPOOLFUNC(pad)	(((GstRealPad *)(pad))->bufferpoolfunc)

#define GST_RPAD_REGIONTYPE(pad)	(((GstRealPad *)(pad))->regiontype)
#define GST_RPAD_OFFSET(pad)		(((GstRealPad *)(pad))->offset)
#define GST_RPAD_LEN(pad)		(((GstRealPad *)(pad))->len)

/* GstGhostPad */
#define GST_GPAD_REALPAD(pad)		(((GstGhostPad *)(pad))->realpad)

/* Generic */
#define GST_PAD_REALIZE(pad)		(GST_IS_REAL_PAD(pad) ? ((GstRealPad *)(pad)) : GST_GPAD_REALPAD(pad))
#define GST_PAD_DIRECTION(pad)		GST_RPAD_DIRECTION(GST_PAD_REALIZE(pad))
#define GST_PAD_CAPS(pad)		GST_RPAD_CAPS(GST_PAD_REALIZE(pad))
#define GST_PAD_PEER(pad)		GST_PAD_CAST(GST_RPAD_PEER(GST_PAD_REALIZE(pad)))

/* Some check functions (unused?) */
#define GST_PAD_IS_CONNECTED(pad)	(GST_PAD_PEER(pad) != NULL)
#define GST_PAD_CAN_PULL(pad)		(GST_IS_REAL_PAD(pad) && GST_REAL_PAD(pad)->gethandler != NULL)
#define GST_PAD_IS_SRC(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SRC)
#define GST_PAD_IS_SINK(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SINK)

/***** PadTemplate *****/
#define GST_TYPE_PADTEMPLATE		(gst_padtemplate_get_type ())
#define GST_PADTEMPLATE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PADTEMPLATE,GstPadTemplate))
#define GST_PADTEMPLATE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PADTEMPLATE,GstPadTemplateClass))
#define GST_IS_PADTEMPLATE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PADTEMPLATE))
#define GST_IS_PADTEMPLATE_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PADTEMPLATE))

typedef enum {
  GST_PAD_ALWAYS,
  GST_PAD_SOMETIMES,
  GST_PAD_REQUEST,
} GstPadPresence;

#define GST_PADTEMPLATE_NAME_TEMPLATE(templ)	(((GstPadTemplate *)(templ))->name_template)
#define GST_PADTEMPLATE_DIRECTION(templ)	(((GstPadTemplate *)(templ))->direction)
#define GST_PADTEMPLATE_PRESENCE(templ)		(((GstPadTemplate *)(templ))->presence)
#define GST_PADTEMPLATE_CAPS(templ)		(((GstPadTemplate *)(templ))->caps)
#define GST_PADTEMPLATE_FIXED(templ)		(((GstPadTemplate *)(templ))->fixed)

#define GST_PADTEMPLATE_IS_FIXED(templ)		(GST_PADTEMPLATE_FIXED(templ) == TRUE)

struct _GstPadTemplate {
  GstObject	  object;

  gchar           *name_template;
  GstPadDirection direction;
  GstPadPresence  presence;
  GstCaps	  *caps;
  gboolean	  fixed;
};

struct _GstPadTemplateClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*pad_created)	(GstPadTemplate *templ, GstPad *pad);
};

#define GST_PADTEMPLATE_NEW(padname, dir, pres, a...) \
  gst_padtemplate_new (                         \
    padname,                                    \
    dir,                                        \
    pres,                                       \
    a ,						\
    NULL)

#define GST_PADTEMPLATE_FACTORY(name, padname, dir, pres, a...)         \
static GstPadTemplate*                          \
name (void)                                     \
{                                               \
  static GstPadTemplate *templ = NULL;       	\
  if (!templ) {                              	\
    templ = GST_PADTEMPLATE_NEW (            	\
      padname,                          	\
      dir,                                      \
      pres,                                     \
      a );                                     \
  }                                             \
  return templ;                              	\
}

#define GST_PADTEMPLATE_GET(fact) (fact)()

GType			gst_pad_get_type			(void);
GType			gst_real_pad_get_type			(void);
GType			gst_ghost_pad_get_type			(void);

GstPad*			gst_pad_new				(gchar *name, GstPadDirection direction);
#define			gst_pad_destroy(pad)			gst_object_destroy (GST_OBJECT (pad))
GstPad*			gst_pad_new_from_template		(GstPadTemplate *templ, gchar *name);

GstPadDirection		gst_pad_get_direction			(GstPad *pad);

void			gst_pad_set_chain_function		(GstPad *pad, GstPadChainFunction chain);
void			gst_pad_set_get_function		(GstPad *pad, GstPadGetFunction get);
void			gst_pad_set_event_function		(GstPad *pad, GstPadEventFunction event);

void			gst_pad_set_getregion_function		(GstPad *pad, GstPadGetRegionFunction getregion);

void			gst_pad_set_connect_function		(GstPad *pad, GstPadConnectFunction connect);
void			gst_pad_set_getcaps_function		(GstPad *pad, GstPadGetCapsFunction getcaps);
void			gst_pad_set_bufferpool_function		(GstPad *pad, GstPadBufferPoolFunction bufpool);

GstCaps*		gst_pad_get_caps			(GstPad *pad);
GstCaps*		gst_pad_get_padtemplate_caps		(GstPad *pad);
gboolean		gst_pad_try_set_caps			(GstPad *pad, GstCaps *caps);
gboolean		gst_pad_check_compatibility		(GstPad *srcpad, GstPad *sinkpad);

void			gst_pad_set_element_private		(GstPad *pad, gpointer priv);
gpointer		gst_pad_get_element_private		(GstPad *pad);

void			gst_pad_set_name			(GstPad *pad, const gchar *name);
const gchar*		gst_pad_get_name			(GstPad *pad);

void			gst_pad_set_parent			(GstPad *pad, GstObject *parent);
GstElement*		gst_pad_get_parent			(GstPad *pad);
GstElement*		gst_pad_get_real_parent			(GstPad *pad);

void			gst_pad_set_sched			(GstPad *pad, GstScheduler *sched);
GstScheduler*		gst_pad_get_sched			(GstPad *pad);
void			gst_pad_unset_sched			(GstPad *pad);

void			gst_pad_add_ghost_pad			(GstPad *pad, GstPad *ghostpad);
void			gst_pad_remove_ghost_pad		(GstPad *pad, GstPad *ghostpad);
GList*			gst_pad_get_ghost_pad_list		(GstPad *pad);

GstPadTemplate*		gst_pad_get_padtemplate			(GstPad *pad);

GstPad*			gst_pad_get_peer			(GstPad *pad);

GstBufferPool*		gst_pad_get_bufferpool			(GstPad *pad);

gboolean                gst_pad_can_connect            		(GstPad *srcpad, GstPad *sinkpad);
gboolean                gst_pad_can_connect_filtered   		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);

gboolean                gst_pad_connect             		(GstPad *srcpad, GstPad *sinkpad);
gboolean                gst_pad_connect_filtered       		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
void			gst_pad_disconnect			(GstPad *srcpad, GstPad *sinkpad);

GstPadConnectReturn     gst_pad_proxy_connect          		(GstPad *pad, GstCaps *caps);
gboolean		gst_pad_reconnect_filtered		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
gboolean		gst_pad_perform_negotiate		(GstPad *srcpad, GstPad *sinkpad);
gboolean		gst_pad_try_reconnect_filtered		(GstPad *srcpad, GstPad *sinkpad, GstCaps *filtercaps);
GstCaps*	     	gst_pad_get_allowed_caps       		(GstPad *pad);
gboolean	     	gst_pad_recalc_allowed_caps    		(GstPad *pad);

#if 1
void			gst_pad_push				(GstPad *pad, GstBuffer *buf);
#else
#define gst_pad_push(pad,buf) G_STMT_START{ \
  if (((GstRealPad *)(pad))->peer->chainhandler) \
    (((GstRealPad *)(pad))->peer->chainhandler)((GstPad *)(((GstRealPad *)(pad))->peer),(buf)); \
}G_STMT_END
#endif
#if 1
GstBuffer*		gst_pad_pull				(GstPad *pad);
GstBuffer*		gst_pad_pullregion			(GstPad *pad, GstRegionType type, 
								 guint64 offset, guint64 len);
#else
#define gst_pad_pull(pad) \
  ( (((GstRealPad *)(pad))->peer->gethandler) ? \
(((GstRealPad *)(pad))->peer->gethandler)((GstPad *)(((GstRealPad *)(pad))->peer)) : \
NULL )
#define gst_pad_pullregion(pad,type,offset,len) \
  ( (((GstRealPad *)(pad))->peer->pullregionfunc) ? \
(((GstRealPad *)(pad))->peer->pullregionfunc)((GstPad *)(((GstRealPad *)(pad))->peer),(type),(offset),(len)) : \
NULL )
#endif

gboolean		gst_pad_send_event			(GstPad *pad, GstEvent *event);
void 			gst_pad_event_default			(GstPad *pad, GstEvent *event);



GstBuffer*		gst_pad_peek				(GstPad *pad);
GstPad*			gst_pad_select				(GList *padlist);
GstPad*			gst_pad_selectv				(GstPad *pad, ...);

#ifndef GST_DISABLE_LOADSAVE
void			gst_pad_load_and_connect		(xmlNodePtr self, GstObject *parent);
#endif


/* ghostpads */
GstPad*			gst_ghost_pad_new			(gchar *name,GstPad *pad);


/* templates and factories */
GType			gst_padtemplate_get_type		(void);

GstPadTemplate*		gst_padtemplate_new			(gchar *name_template,
		                                        	 GstPadDirection direction, GstPadPresence presence,
								 GstCaps *caps, ...);

GstCaps*		gst_padtemplate_get_caps		(GstPadTemplate *templ);
GstCaps*		gst_padtemplate_get_caps_by_name	(GstPadTemplate *templ, const gchar *name);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr		gst_padtemplate_save_thyself		(GstPadTemplate *templ, xmlNodePtr parent);
GstPadTemplate*		gst_padtemplate_load_thyself		(xmlNodePtr parent);
#endif

xmlNodePtr              gst_pad_ghost_save_thyself   		(GstPad *pad,
						     		 GstElement *bin,
						     		 xmlNodePtr parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PAD_H__ */

