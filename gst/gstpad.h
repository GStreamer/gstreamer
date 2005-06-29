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
#include <gst/gstquery.h>
#include <gst/gstqueryutils.h>
#include <gst/gsttask.h>


G_BEGIN_DECLS

GST_EXPORT GType _gst_pad_type;

/*
 * Pad base class
 */
#define GST_TYPE_PAD			(_gst_pad_type)
#define GST_IS_PAD(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PAD))
#define GST_IS_PAD_FAST(obj)		(G_OBJECT_TYPE(obj) == GST_TYPE_PAD) /* necessary? */
#define GST_IS_PAD_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD))
#define GST_PAD(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PAD, GstPad))
#define GST_PAD_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD, GstPadClass))
#define GST_PAD_CAST(obj)		((GstPad*)(obj))


/* why are these in gsttypes, again? */
/*typedef struct _GstPad GstPad;*/
/*typedef struct _GstPadClass GstPadClass;*/
/*typedef struct _GstPadTemplate GstPadTemplate;*/
/*typedef struct _GstPadTemplateClass GstPadTemplateClass;*/
typedef struct _GstStaticPadTemplate GstStaticPadTemplate;

typedef enum {
  GST_PAD_LINK_NOSCHED          = -5,	/* pads cannot cooperate in scheduling */
  GST_PAD_LINK_NOFORMAT         = -4,	/* pads do not have common format */
  GST_PAD_LINK_REFUSED          = -3,	/* refused for some reason */
  GST_PAD_LINK_WRONG_DIRECTION  = -2,	/* pads have wrong direction */
  GST_PAD_LINK_WAS_LINKED       = -1,	/* pad was already linked */
  GST_PAD_LINK_OK               =  0,	/* link ok */
} GstPadLinkReturn;

#define GST_PAD_LINK_FAILED(ret) ((ret) < GST_PAD_LINK_OK)
#define GST_PAD_LINK_SUCCESSFUL(ret) ((ret) >= GST_PAD_LINK_OK)

typedef enum {
  GST_FLOW_OK		  =  0,		/* data passing was ok */
  GST_FLOW_RESEND	  =  1,		/* resend buffer, possibly with new caps */
  GST_FLOW_ERROR	  = -1,		/* some (fatal) error occured */
  GST_FLOW_NOT_LINKED     = -2,		/* pad is not linked */
  GST_FLOW_NOT_NEGOTIATED = -3,		/* pad is not negotiated */
  GST_FLOW_WRONG_STATE    = -4,		/* pad is in wrong state */
  GST_FLOW_UNEXPECTED     = -5,		/* did not expect anything, this is not fatal */
  GST_FLOW_NOT_SUPPORTED  = -6		/* function not supported */
} GstFlowReturn;

typedef enum {
  GST_ACTIVATE_NONE,
  GST_ACTIVATE_PUSH,
  GST_ACTIVATE_PULL,
} GstActivateMode;

#define GST_PAD_MODE_ACTIVATE(mode) ((mode) != GST_ACTIVATE_NONE)

/* pad states */
typedef gboolean		(*GstPadActivateFunction)	(GstPad *pad);
typedef gboolean		(*GstPadActivateModeFunction)	(GstPad *pad, gboolean active);

/* data passing */
typedef GstFlowReturn		(*GstPadChainFunction)		(GstPad *pad, GstBuffer *buffer);
typedef GstFlowReturn		(*GstPadGetRangeFunction)	(GstPad *pad, guint64 offset,
		                                                 guint length, GstBuffer **buffer);
typedef gboolean		(*GstPadEventFunction)		(GstPad *pad, GstEvent *event);

/* deprecate me, check range should use seeking query */
typedef gboolean		(*GstPadCheckGetRangeFunction)	(GstPad *pad);

/* internal links */
typedef GList*			(*GstPadIntLinkFunction)	(GstPad *pad);

/* generic query function */
typedef const GstQueryType*	(*GstPadQueryTypeFunction)	(GstPad *pad);
typedef gboolean		(*GstPadQueryFunction)		(GstPad *pad, GstQuery *query);

/* linking */
typedef GstPadLinkReturn	(*GstPadLinkFunction)		(GstPad *pad, GstPad *peer);
typedef void			(*GstPadUnlinkFunction)		(GstPad *pad);

/* caps nego */
typedef GstCaps*		(*GstPadGetCapsFunction)	(GstPad *pad);
typedef gboolean		(*GstPadSetCapsFunction)	(GstPad *pad, GstCaps *caps);
typedef gboolean		(*GstPadAcceptCapsFunction)	(GstPad *pad, GstCaps *caps);
typedef GstCaps*		(*GstPadFixateCapsFunction)	(GstPad *pad, GstCaps *caps);
typedef GstFlowReturn		(*GstPadBufferAllocFunction)	(GstPad *pad, guint64 offset, guint size,
								 GstCaps *caps, GstBuffer **buf);
/* misc */
typedef gboolean		(*GstPadDispatcherFunction)	(GstPad *pad, gpointer data);

typedef void			(*GstPadBlockCallback)		(GstPad *pad, gboolean blocked, gpointer user_data);

typedef enum {
  GST_PAD_UNKNOWN,
  GST_PAD_SRC,
  GST_PAD_SINK
} GstPadDirection;

typedef enum {
  GST_PAD_BLOCKED		= GST_OBJECT_FLAG_LAST,
  GST_PAD_FLUSHING,
  GST_PAD_IN_GETCAPS,
  GST_PAD_IN_SETCAPS,

  GST_PAD_FLAG_LAST		= GST_OBJECT_FLAG_LAST + 8
} GstPadFlags;

struct _GstPad {
  GstObject			object;

  gpointer			element_private;

  GstPadTemplate		*padtemplate;

  /* direction cannot change after creating the pad */
  GstPadDirection		 direction;

  /*< public >*/ /* with STREAM_LOCK */
  /* streaming rec_lock */
  GStaticRecMutex		*stream_rec_lock;
  GstTask			*task;
  /*< public >*/ /* with PREROLL_LOCK */
  GMutex			*preroll_lock;
  GCond				*preroll_cond;

  /*< public >*/ /* with LOCK */
  /* block cond, mutex is from the object */
  GCond				*block_cond;
  GstPadBlockCallback		 block_callback;
  gpointer			 block_data;

  /* the pad capabilities */
  GstCaps			*caps;
  GstPadGetCapsFunction		getcapsfunc;
  GstPadSetCapsFunction		setcapsfunc;
  GstPadAcceptCapsFunction	 acceptcapsfunc;
  GstPadFixateCapsFunction	 fixatecapsfunc;

  GstPadActivateFunction	 activatefunc;
  GstPadActivateModeFunction	 activatepushfunc;
  GstPadActivateModeFunction	 activatepullfunc;

  /* pad link */
  GstPadLinkFunction		 linkfunc;
  GstPadUnlinkFunction		 unlinkfunc;
  GstPad			*peer;

  gpointer			 sched_private;

  /* data transport functions */
  GstPadChainFunction		 chainfunc;
  GstPadCheckGetRangeFunction	 checkgetrangefunc;
  GstPadGetRangeFunction	 getrangefunc;
  GstPadEventFunction		 eventfunc;

  GstActivateMode		 mode;

  /* generic query method */
  GstPadQueryTypeFunction	 querytypefunc;
  GstPadQueryFunction		 queryfunc;

  /* internal links */
  GstPadIntLinkFunction		 intlinkfunc;

  GstPadBufferAllocFunction      bufferallocfunc;

  /* whether to emit signals for have-data */
  gint				 emit_buffer_signals, emit_event_signals;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPadClass {
  GstObjectClass	parent_class;

  /* signal callbacks */
  void		(*linked)		(GstPad *pad, GstPad *peer);
  void		(*unlinked)		(GstPad *pad, GstPad *peer);
  void		(*request_link)		(GstPad *pad);
  gboolean	(*have_data)		(GstPad *pad, GstMiniObject *data);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};


/***** helper macros *****/
/* GstPad */
#define GST_PAD_NAME(pad)		(GST_OBJECT_NAME(pad))
#define GST_PAD_PARENT(pad)		(GST_ELEMENT_CAST(GST_OBJECT_PARENT(pad)))
#define GST_PAD_ELEMENT_PRIVATE(pad)	(GST_PAD_CAST(pad)->element_private)
#define GST_PAD_PAD_TEMPLATE(pad)	(GST_PAD_CAST(pad)->padtemplate)
#define GST_PAD_DIRECTION(pad)		(GST_PAD_CAST(pad)->direction)
#define GST_PAD_TASK(pad)		(GST_PAD_CAST(pad)->task)
#define GST_PAD_ACTIVATE_MODE(pad)	(GST_PAD_CAST(pad)->mode)

#define GST_PAD_ACTIVATEFUNC(pad)	(GST_PAD_CAST(pad)->activatefunc)
#define GST_PAD_ACTIVATEPUSHFUNC(pad)	(GST_PAD_CAST(pad)->activatepushfunc)
#define GST_PAD_ACTIVATEPULLFUNC(pad)	(GST_PAD_CAST(pad)->activatepullfunc)
#define GST_PAD_CHAINFUNC(pad)		(GST_PAD_CAST(pad)->chainfunc)
#define GST_PAD_CHECKGETRANGEFUNC(pad)	(GST_PAD_CAST(pad)->checkgetrangefunc)
#define GST_PAD_GETRANGEFUNC(pad)	(GST_PAD_CAST(pad)->getrangefunc)
#define GST_PAD_EVENTFUNC(pad)		(GST_PAD_CAST(pad)->eventfunc)
#define GST_PAD_QUERYTYPEFUNC(pad)	(GST_PAD_CAST(pad)->querytypefunc)
#define GST_PAD_QUERYFUNC(pad)		(GST_PAD_CAST(pad)->queryfunc)
#define GST_PAD_INTLINKFUNC(pad)	(GST_PAD_CAST(pad)->intlinkfunc)

#define GST_PAD_PEER(pad)		(GST_PAD_CAST(pad)->peer)
#define GST_PAD_LINKFUNC(pad)		(GST_PAD_CAST(pad)->linkfunc)
#define GST_PAD_UNLINKFUNC(pad)		(GST_PAD_CAST(pad)->unlinkfunc)

#define GST_PAD_CAPS(pad)		(GST_PAD_CAST(pad)->caps)
#define GST_PAD_GETCAPSFUNC(pad)	(GST_PAD_CAST(pad)->getcapsfunc)
#define GST_PAD_SETCAPSFUNC(pad)	(GST_PAD_CAST(pad)->setcapsfunc)
#define GST_PAD_ACCEPTCAPSFUNC(pad)	(GST_PAD_CAST(pad)->acceptcapsfunc)
#define GST_PAD_FIXATECAPSFUNC(pad)	(GST_PAD_CAST(pad)->fixatecapsfunc)

#define GST_PAD_BUFFERALLOCFUNC(pad)	(GST_PAD_CAST(pad)->bufferallocfunc)

#define GST_PAD_IS_LINKED(pad)		(GST_PAD_PEER(pad) != NULL)
#define GST_PAD_IS_BLOCKED(pad)	(GST_FLAG_IS_SET (pad, GST_PAD_BLOCKED))
#define GST_PAD_IS_FLUSHING(pad)	(GST_FLAG_IS_SET (pad, GST_PAD_FLUSHING))
#define GST_PAD_IS_IN_GETCAPS(pad)	(GST_FLAG_IS_SET (pad, GST_PAD_IN_GETCAPS))
#define GST_PAD_IS_IN_SETCAPS(pad)	(GST_FLAG_IS_SET (pad, GST_PAD_IN_SETCAPS))
#define GST_PAD_IS_USABLE(pad)		(GST_PAD_IS_LINKED (pad) && \
		                         !GST_PAD_IS_FLUSHING(pad) && !GST_PAD_IS_FLUSHING(GST_PAD_PEER (pad)))
#define GST_PAD_IS_SRC(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SRC)
#define GST_PAD_IS_SINK(pad)		(GST_PAD_DIRECTION(pad) == GST_PAD_SINK)

#define GST_PAD_SET_FLUSHING(pad)	(GST_FLAG_SET (pad, GST_PAD_FLUSHING))
#define GST_PAD_UNSET_FLUSHING(pad)	(GST_FLAG_UNSET (pad, GST_PAD_FLUSHING))

#define GST_STREAM_GET_LOCK(pad)        (GST_PAD_CAST(pad)->stream_rec_lock)
#define GST_STREAM_LOCK(pad)            (g_static_rec_mutex_lock(GST_STREAM_GET_LOCK(pad)))
#define GST_STREAM_TRYLOCK(pad)         (g_static_rec_mutex_trylock(GST_STREAM_GET_LOCK(pad)))
#define GST_STREAM_UNLOCK(pad)          (g_static_rec_mutex_unlock(GST_STREAM_GET_LOCK(pad)))
#define GST_STREAM_UNLOCK_FULL(pad)     (g_static_rec_mutex_unlock_full(GST_STREAM_GET_LOCK(pad)))
#define GST_STREAM_LOCK_FULL(pad,t)     (g_static_rec_mutex_lock_full(GST_STREAM_GET_LOCK(pad), t))

#define GST_PREROLL_GET_LOCK(pad)       (GST_PAD_CAST(pad)->preroll_lock)
#define GST_PREROLL_LOCK(pad)           (g_mutex_lock(GST_PREROLL_GET_LOCK(pad)))
#define GST_PREROLL_TRYLOCK(pad)        (g_mutex_trylock(GST_PREROLL_GET_LOCK(pad)))
#define GST_PREROLL_UNLOCK(pad)         (g_mutex_unlock(GST_PREROLL_GET_LOCK(pad)))
#define GST_PREROLL_GET_COND(pad)       (GST_PAD_CAST(pad)->preroll_cond)
#define GST_PREROLL_WAIT(pad)           g_cond_wait (GST_PREROLL_GET_COND (pad), GST_PREROLL_GET_LOCK (pad))
#define GST_PREROLL_TIMED_WAIT(pad, timeval) g_cond_timed_wait (GST_PREROLL_GET_COND (pad), GST_PREROLL_GET_LOCK (pad),\
		                             timeval)
#define GST_PREROLL_SIGNAL(pad)         g_cond_signal (GST_PREROLL_GET_COND (pad));
#define GST_PREROLL_BROADCAST(pad)      g_cond_broadcast (GST_PREROLL_GET_COND (pad));

#define GST_PAD_BLOCK_GET_COND(pad)     (GST_PAD_CAST(pad)->block_cond)
#define GST_PAD_BLOCK_WAIT(pad)         (g_cond_wait(GST_PAD_BLOCK_GET_COND (pad), GST_GET_LOCK (pad)))
#define GST_PAD_BLOCK_SIGNAL(pad)       (g_cond_signal(GST_PAD_BLOCK_GET_COND (pad)))

/***** PadTemplate *****/
#define GST_TYPE_PAD_TEMPLATE		(gst_pad_template_get_type ())
#define GST_PAD_TEMPLATE(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PAD_TEMPLATE,GstPadTemplate))
#define GST_PAD_TEMPLATE_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PAD_TEMPLATE,GstPadTemplateClass))
#define GST_IS_PAD_TEMPLATE(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PAD_TEMPLATE))
#define GST_IS_PAD_TEMPLATE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PAD_TEMPLATE))

typedef enum {
  GST_PAD_ALWAYS,
  GST_PAD_SOMETIMES,
  GST_PAD_REQUEST
} GstPadPresence;

#define GST_PAD_TEMPLATE_NAME_TEMPLATE(templ)	(((GstPadTemplate *)(templ))->name_template)
#define GST_PAD_TEMPLATE_DIRECTION(templ)	(((GstPadTemplate *)(templ))->direction)
#define GST_PAD_TEMPLATE_PRESENCE(templ)	(((GstPadTemplate *)(templ))->presence)
#define GST_PAD_TEMPLATE_CAPS(templ)		(((GstPadTemplate *)(templ))->caps)

typedef enum {
  GST_PAD_TEMPLATE_FIXED	= GST_OBJECT_FLAG_LAST,

  GST_PAD_TEMPLATE_FLAG_LAST	= GST_OBJECT_FLAG_LAST + 4
} GstPadTemplateFlags;

#define GST_PAD_TEMPLATE_IS_FIXED(templ)	(GST_FLAG_IS_SET(templ, GST_PAD_TEMPLATE_FIXED))

struct _GstPadTemplate {
  GstObject	   object;

  gchar           *name_template;
  GstPadDirection  direction;
  GstPadPresence   presence;
  GstCaps	  *caps;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstPadTemplateClass {
  GstObjectClass   parent_class;

  /* signal callbacks */
  void (*pad_created)	(GstPadTemplate *templ, GstPad *pad);

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstStaticPadTemplate {
  gchar           *name_template;
  GstPadDirection  direction;
  GstPadPresence   presence;
  GstStaticCaps   static_caps;
};

#define GST_STATIC_PAD_TEMPLATE(padname, dir, pres, caps) \
  { \
  /* name_template */    padname, \
  /* direction */        dir, \
  /* presence */         pres, \
  /* caps */             caps \
  }


GType			gst_pad_get_type			(void);

/* creating pads */
GstPad*			gst_pad_new				(const gchar *name, GstPadDirection direction);
GstPad*			gst_pad_new_from_template		(GstPadTemplate *templ, const gchar *name);

#define gst_pad_get_name(pad) gst_object_get_name (GST_OBJECT_CAST (pad))
GstElement*		gst_pad_get_parent			(GstPad *pad);

GstPadDirection		gst_pad_get_direction			(GstPad *pad);

gboolean		gst_pad_set_active			(GstPad *pad, gboolean active);
gboolean		gst_pad_is_active			(GstPad *pad);
gboolean		gst_pad_activate_pull			(GstPad *pad, gboolean active);
gboolean		gst_pad_activate_push			(GstPad *pad, gboolean active);

gboolean		gst_pad_set_blocked			(GstPad *pad, gboolean blocked);
gboolean		gst_pad_set_blocked_async		(GstPad *pad, gboolean blocked,
								 GstPadBlockCallback callback, gpointer user_data);
gboolean		gst_pad_is_blocked			(GstPad *pad);

void			gst_pad_set_element_private		(GstPad *pad, gpointer priv);
gpointer		gst_pad_get_element_private		(GstPad *pad);

GstPadTemplate*		gst_pad_get_pad_template		(GstPad *pad);

void			gst_pad_set_bufferalloc_function	(GstPad *pad, GstPadBufferAllocFunction bufalloc);
GstFlowReturn		gst_pad_alloc_buffer			(GstPad *pad, guint64 offset, gint size,
								 GstCaps *caps, GstBuffer **buf);

/* data passing setup functions */
void			gst_pad_set_activate_function		(GstPad *pad, GstPadActivateFunction activate);
void			gst_pad_set_activatepull_function	(GstPad *pad, GstPadActivateModeFunction activatepull);
void			gst_pad_set_activatepush_function	(GstPad *pad, GstPadActivateModeFunction activatepush);
void			gst_pad_set_chain_function		(GstPad *pad, GstPadChainFunction chain);
void			gst_pad_set_getrange_function		(GstPad *pad, GstPadGetRangeFunction get);
void			gst_pad_set_checkgetrange_function	(GstPad *pad, GstPadCheckGetRangeFunction check);
void			gst_pad_set_event_function		(GstPad *pad, GstPadEventFunction event);

/* pad links */
void			gst_pad_set_link_function		(GstPad *pad, GstPadLinkFunction link);
void			gst_pad_set_unlink_function		(GstPad *pad, GstPadUnlinkFunction unlink);

GstPadLinkReturn        gst_pad_link				(GstPad *srcpad, GstPad *sinkpad);
gboolean		gst_pad_unlink				(GstPad *srcpad, GstPad *sinkpad);
gboolean		gst_pad_is_linked			(GstPad *pad);

GstPad*			gst_pad_get_peer			(GstPad *pad);

/* capsnego functions */
void			gst_pad_set_getcaps_function		(GstPad *pad, GstPadGetCapsFunction getcaps);
void			gst_pad_set_acceptcaps_function		(GstPad *pad, GstPadAcceptCapsFunction acceptcaps);
void			gst_pad_set_fixatecaps_function		(GstPad *pad, GstPadFixateCapsFunction fixatecaps);
void			gst_pad_set_setcaps_function		(GstPad *pad, GstPadSetCapsFunction setcaps);

G_CONST_RETURN GstCaps*	gst_pad_get_pad_template_caps		(GstPad *pad);

/* capsnego function for connected/unconnected pads */
GstCaps *		gst_pad_get_caps			(GstPad * pad);
GstCaps*		gst_pad_fixate_caps			(GstPad * pad, GstCaps *caps);
gboolean		gst_pad_accept_caps			(GstPad * pad, GstCaps *caps);
gboolean		gst_pad_set_caps			(GstPad * pad, GstCaps *caps);

GstCaps *		gst_pad_peer_get_caps			(GstPad * pad);
gboolean		gst_pad_peer_accept_caps		(GstPad * pad, GstCaps *caps);

/* capsnego for connected pads */
GstCaps *		gst_pad_get_allowed_caps		(GstPad * srcpad);
GstCaps *		gst_pad_get_negotiated_caps		(GstPad * pad);

/* data passing functions to peer */
GstFlowReturn		gst_pad_push				(GstPad *pad, GstBuffer *buffer);
gboolean		gst_pad_check_pull_range		(GstPad *pad);
GstFlowReturn		gst_pad_pull_range			(GstPad *pad, guint64 offset, guint size,
								 GstBuffer **buffer);
gboolean		gst_pad_push_event			(GstPad *pad, GstEvent *event);
gboolean		gst_pad_event_default			(GstPad *pad, GstEvent *event);

/* data passing functions on pad */
GstFlowReturn		gst_pad_chain				(GstPad *pad, GstBuffer *buffer);
GstFlowReturn		gst_pad_get_range			(GstPad *pad, guint64 offset, guint size,
								 GstBuffer **buffer);
gboolean		gst_pad_send_event			(GstPad *pad, GstEvent *event);

/* pad tasks */
gboolean		gst_pad_start_task			(GstPad *pad, GstTaskFunction func,
								 gpointer data);
gboolean		gst_pad_pause_task			(GstPad *pad);
gboolean		gst_pad_stop_task			(GstPad *pad);

/* internal links */
void			gst_pad_set_internal_link_function	(GstPad *pad, GstPadIntLinkFunction intlink);
GList*			gst_pad_get_internal_links		(GstPad *pad);
GList*			gst_pad_get_internal_links_default	(GstPad *pad);

/* generic query function */
void			gst_pad_set_query_type_function		(GstPad *pad, GstPadQueryTypeFunction type_func);
G_CONST_RETURN GstQueryType*
			gst_pad_get_query_types			(GstPad *pad);
G_CONST_RETURN GstQueryType*
			gst_pad_get_query_types_default		(GstPad *pad);

gboolean		gst_pad_query				(GstPad *pad, GstQuery *query);
void			gst_pad_set_query_function		(GstPad *pad, GstPadQueryFunction query);
gboolean		gst_pad_query_default			(GstPad *pad, GstQuery *query);

/* misc helper functions */
gboolean		gst_pad_dispatcher			(GstPad *pad, GstPadDispatcherFunction dispatch,
								 gpointer data);

#ifndef GST_DISABLE_LOADSAVE
void			gst_pad_load_and_link			(xmlNodePtr self, GstObject *parent);
#endif


/* templates and factories */
GType			gst_pad_template_get_type		(void);

GstPadTemplate*		gst_pad_template_new			(const gchar *name_template,
								 GstPadDirection direction, GstPadPresence presence,
								 GstCaps *caps);

GstPadTemplate *	gst_static_pad_template_get             (GstStaticPadTemplate *pad_template);
GstCaps*		gst_static_pad_template_get_caps	(GstStaticPadTemplate *templ);
GstCaps*		gst_pad_template_get_caps		(GstPadTemplate *templ);


G_END_DECLS

#endif /* __GST_PAD_H__ */
