/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008 Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
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

#ifndef __GST_QT_MUX_H__
#define __GST_QT_MUX_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

#include "fourcc.h"
#include "atoms.h"
#include "gstqtmuxmap.h"

G_BEGIN_DECLS

#define GST_TYPE_QT_MUX (gst_qt_mux_get_type())
#define GST_QT_MUX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QT_MUX, GstQTMux))
#define GST_QT_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QT_MUX, GstQTMux))
#define GST_IS_QT_MUX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QT_MUX))
#define GST_IS_QT_MUX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QT_MUX))
#define GST_QT_MUX_CAST(obj) ((GstQTMux*)(obj))


typedef struct _GstQTMux GstQTMux;
typedef struct _GstQTMuxClass GstQTMuxClass;

typedef struct _GstQTPad
{
  GstCollectData collect;       /* we extend the CollectData */

  /* fourcc id of stream */
  guint32 fourcc;
  /* whether using format that have out of order buffers */
  gboolean is_out_of_order;
  /* whether upstream provides valid PTS data */
  gboolean have_dts;
  /* if not 0, track with constant sized samples, e.g. raw audio */
  guint sample_size;
  /* make sync table entry */
  gboolean sync;

  GstBuffer *last_buf;
  /* dts of last_buf */
  GstClockTime last_dts;

  /* all the atom and chunk book-keeping is delegated here
   * unowned/uncounted reference, parent MOOV owns */
  AtomTRAK *trak;
} GstQTPad;

typedef enum _GstQTMuxState
{
  GST_QT_MUX_STATE_NONE,
  GST_QT_MUX_STATE_STARTED,
  GST_QT_MUX_STATE_DATA,
  GST_QT_MUX_STATE_EOS
} GstQTMuxState;

struct _GstQTMux
{
  GstElement element;

  GstPad *srcpad;
  GstCollectPads *collect;

  /* state */
  GstQTMuxState state;

  /* size of header (prefix, atoms (ftyp, mdat)) */
  guint64 header_size;
  /* accumulated size of raw media data (a priori not including mdat header) */
  guint64 mdat_size;
  /* position of mdat extended size field (for later updating) */
  guint64 mdat_pos;

  /* atom helper objects */
  AtomsContext *context;
  AtomFTYP *ftyp;
  AtomMOOV *moov;

  /* fast start */
  FILE *fast_start_file;

  GstTagList *tags;

  /* properties */
  guint32 timescale;
  AtomsTreeFlavor flavor;
  gboolean fast_start;
  gboolean large_file;
  gboolean guess_pts;
  gchar *fast_start_file_path;

  /* for collect pads event handling function */
  GstPadEventFunction collect_event;
};

struct _GstQTMuxClass
{
  GstElementClass parent_class;

  GstQTMuxFormat format;
};

/* type register helper struct */
typedef struct _GstQTMuxClassParams
{
  GstQTMuxFormatProp *prop;
  GstCaps *src_caps;
  GstCaps *video_sink_caps;
  GstCaps *audio_sink_caps;
} GstQTMuxClassParams;

#define GST_QT_MUX_PARAMS_QDATA g_quark_from_static_string("qt-mux-params")

GType gst_qt_mux_get_type (void);

G_END_DECLS

#endif /* __GST_QT_MUX_H__ */
