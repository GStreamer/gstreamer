/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstevent.h: Header for GstEvent subsystem
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


#ifndef __GST_EVENT_H__
#define __GST_EVENT_H__

#include <gst/gstminiobject.h>
#include <gst/gstformat.h>
#include <gst/gstobject.h>
#include <gst/gststructure.h>

G_BEGIN_DECLS

/**
 * GstEventType:
 * @GST_EVENT_UNKNOWN: 
 * @GST_EVENT_EOS:
 * @GST_EVENT_FLUSH:
 * @GST_EVENT_DISCONTINUOUS:
 * @GST_EVENT_QOS:
 * @GST_EVENT_SEEK:
 * @GST_EVENT_SIZE:
 * @GST_EVENT_RATE:
 * @GST_EVENT_NAVIGATION:
 * @GST_EVENT_TAG:
 */
typedef enum {
  GST_EVENT_UNKNOWN		= 0,
  GST_EVENT_EOS			= 1,
  GST_EVENT_FLUSH		= 2,
  GST_EVENT_DISCONTINUOUS	= 3,
  GST_EVENT_QOS			= 4,
  GST_EVENT_SEEK		= 5,
  GST_EVENT_SIZE		= 8,
  GST_EVENT_RATE		= 9,
  GST_EVENT_NAVIGATION		= 10,
  GST_EVENT_TAG			= 11
} GstEventType;
#define GST_EVENT_ANY GST_EVENT_NAVIGATION

#define GST_EVENT_TRACE_NAME	"GstEvent"

typedef struct _GstEvent GstEvent;
typedef struct _GstEventClass GstEventClass;

#define GST_TYPE_EVENT			       (gst_event_get_type())
#define GST_IS_EVENT(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EVENT))
#define GST_IS_EVENT_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_EVENT))
#define GST_EVENT_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_EVENT, GstEventClass))
#define GST_EVENT(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_EVENT, GstEvent))
#define GST_EVENT_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_EVENT, GstEventClass))

#define GST_EVENT_TYPE(event)		(GST_EVENT(event)->type)
#define GST_EVENT_TIMESTAMP(event)	(GST_EVENT(event)->timestamp)
#define GST_EVENT_SRC(event)		(GST_EVENT(event)->src)

#define GST_EVENT_IS_INTERRUPT(event) (GST_EVENT_TYPE (event) == GST_EVENT_INTERRUPT)

#define GST_SEEK_FORMAT_SHIFT	0
#define GST_SEEK_METHOD_SHIFT	16
#define GST_SEEK_FLAGS_SHIFT	20
#define GST_SEEK_FORMAT_MASK	0x0000ffff
#define GST_SEEK_METHOD_MASK	0x000f0000
#define GST_SEEK_FLAGS_MASK	0xfff00000

typedef enum {
  GST_EVENT_FLAG_NONE		= 0,

  /* indicates negative rates are supported */
  GST_RATE_FLAG_NEGATIVE	= (1 << 1)
} GstEventFlag;

typedef struct
{
  GstEventType	type;
  GstEventFlag	flags;
} GstEventMask;

#ifdef G_HAVE_ISO_VARARGS
#define GST_EVENT_MASK_FUNCTION(type,functionname, ...)      \
static const GstEventMask*                              \
functionname (type pad)					\
{							\
  static const GstEventMask masks[] = {                 \
    __VA_ARGS__,					\
    { 0, }						\
  };							\
  return masks;						\
}
#elif defined(G_HAVE_GNUC_VARARGS)
#define GST_EVENT_MASK_FUNCTION(type,functionname, a...)     \
static const GstEventMask*                              \
functionname (type pad)					\
{							\
  static const GstEventMask masks[] = {                 \
    a,							\
    { 0, }						\
  };							\
  return masks;						\
}
#endif

/* seek events, extends GstEventFlag */
typedef enum {
  /* | with some format */
  /* | with one of these */
  GST_SEEK_METHOD_CUR		= (1 << (GST_SEEK_METHOD_SHIFT + 0)),
  GST_SEEK_METHOD_SET		= (1 << (GST_SEEK_METHOD_SHIFT + 1)),
  GST_SEEK_METHOD_END		= (1 << (GST_SEEK_METHOD_SHIFT + 2)),

  /* | with optional seek flags */
  /* seek flags */
  GST_SEEK_FLAG_FLUSH		= (1 << (GST_SEEK_FLAGS_SHIFT + 0)),
  GST_SEEK_FLAG_ACCURATE	= (1 << (GST_SEEK_FLAGS_SHIFT + 1)),
  GST_SEEK_FLAG_KEY_UNIT	= (1 << (GST_SEEK_FLAGS_SHIFT + 2)),
  GST_SEEK_FLAG_SEGMENT_LOOP	= (1 << (GST_SEEK_FLAGS_SHIFT + 3))
	
} GstSeekType;

typedef enum {
  GST_SEEK_CERTAIN,
  GST_SEEK_FUZZY
} GstSeekAccuracy;

typedef struct
{
  GstFormat	format;
  gint64	start_value;
  gint64	end_value;
} GstFormatValue;

#define GST_EVENT_SEEK_TYPE(event)		(GST_EVENT(event)->event_data.seek.type)
#define GST_EVENT_SEEK_FORMAT(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FORMAT_MASK)
#define GST_EVENT_SEEK_METHOD(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_METHOD_MASK)
#define GST_EVENT_SEEK_FLAGS(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FLAGS_MASK)
#define GST_EVENT_SEEK_OFFSET(event)		(GST_EVENT(event)->event_data.seek.offset)
#define GST_EVENT_SEEK_ENDOFFSET(event)		(GST_EVENT(event)->event_data.seek.endoffset)
#define GST_EVENT_SEEK_ACCURACY(event)		(GST_EVENT(event)->event_data.seek.accuracy)

#define GST_EVENT_DISCONT_RATE(event)		(GST_EVENT(event)->event_data.discont.rate)
#define GST_EVENT_DISCONT_OFFSET(event,i)	(GST_EVENT(event)->event_data.discont.offsets[i])
#define GST_EVENT_DISCONT_OFFSET_LEN(event)	(GST_EVENT(event)->event_data.discont.noffsets)

#define GST_EVENT_FLUSH_DONE(event)		(GST_EVENT(event)->event_data.flush.done)

#define GST_EVENT_SIZE_FORMAT(event)		(GST_EVENT(event)->event_data.size.format)
#define GST_EVENT_SIZE_VALUE(event)		(GST_EVENT(event)->event_data.size.value)

#define GST_EVENT_RATE_VALUE(event)		(GST_EVENT(event)->event_data.rate.value)

struct _GstEvent {
  GstMiniObject mini_object;

  /*< public >*/ /* with COW */
  GstEventType  type;
  guint64	timestamp;
  GstObject	*src;

  union {
    struct {
      GstSeekType	type;
      gint64		offset;
      gint64		endoffset;
      GstSeekAccuracy	accuracy;
    } seek;
    struct {
      GstFormatValue	offsets[8];
      gint		noffsets;
      gdouble		rate;
    } discont;
    struct {
      gboolean		done;
    } flush;
    struct {
      GstFormat		format;
      gint64		value;
    } size;
    struct {
      gdouble		value;
    } rate;
    struct {
      GstStructure	*structure;
    } structure;
  } event_data;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstEventClass {
  GstMiniObjectClass mini_object_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

void		_gst_event_initialize		(void);
	
GType		gst_event_get_type		(void);
GstEvent*	gst_event_new			(GstEventType type);

/* refcounting */
#define         gst_event_ref(ev)		GST_EVENT (gst_mini_object_ref (GST_MINI_OBJECT (ev)))
#define         gst_event_unref(ev)		gst_mini_object_unref (GST_MINI_OBJECT (ev))
/* copy buffer */
#define         gst_event_copy(ev)		GST_EVENT (gst_mini_object_copy (GST_MINI_OBJECT (ev)))

gboolean	gst_event_masks_contains	(const GstEventMask *masks, GstEventMask *mask);

/* seek event */
GstEvent*	gst_event_new_seek		(GstSeekType type, gint64 offset);

GstEvent*	gst_event_new_segment_seek	(GstSeekType type, gint64 start, gint64 stop);


/* size events */
GstEvent*	gst_event_new_size		(GstFormat format, gint64 value);

/* discontinous event */
GstEvent*	gst_event_new_discontinuous	(gdouble rate,
						 GstFormat format1, ...);
GstEvent*	gst_event_new_discontinuous_valist	(gdouble rate,
						 GstFormat format1,
						 va_list var_args);
gboolean	gst_event_discont_get_value	(GstEvent *event, GstFormat format, 
						 gint64 *start_value, gint64 *end_value);

#define		gst_event_new_filler()		gst_event_new(GST_EVENT_FILLER)
#define		gst_event_new_eos()		gst_event_new(GST_EVENT_EOS)

/* flush events */
GstEvent*	gst_event_new_flush 		(gboolean done);

G_END_DECLS

#endif /* __GST_EVENT_H__ */
