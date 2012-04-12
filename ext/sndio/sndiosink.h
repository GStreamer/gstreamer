/*
 * Copyright (C) <2008> Jacob Meuser <jakemsr@sdf.lonestar.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#ifndef __GST_SNDIOSINK_H__
#define __GST_SNDIOSINK_H__

#include <sndio.h>

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

G_BEGIN_DECLS

#define GST_TYPE_SNDIOSINK \
  (gst_sndiosink_get_type())
#define GST_SNDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SNDIOSINK,GstSndioSink))
#define GST_SNDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SNDIOSINK,GstSndioSinkClass))
#define GST_IS_SNDIOSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SNDIOSINK))
#define GST_IS_SNDIOSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SNDIOSINK))

typedef struct _GstSndioSink GstSndioSink;
typedef struct _GstSndioSinkClass GstSndioSinkClass;

struct _GstSndioSink {
  GstAudioSink   sink;

  struct sio_hdl *hdl;
  gchar    *host;

  /* bytes per frame */
  int bpf;

  /* frames counts */
  volatile long long realpos;
  volatile long long playpos;
  volatile guint latency;

  GstCaps  *cur_caps;
};

struct _GstSndioSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_sndiosink_get_type (void);

G_END_DECLS

#endif /* __GST_SNDIOSINK_H__ */
