/* GStreamer
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


#ifndef __CDPARANOIA_H__
#define __CDPARANOIA_H__


#include <glib.h>
#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define size16 gint16
#define size32 gint32

#ifdef CDPARANOIA_HEADERS_IN_DIR
  #include <cdda/cdda_interface.h>
  #include <cdda/cdda_paranoia.h>
#else
  #include <cdda_interface.h>
  #include <cdda_paranoia.h>
#endif
  


/*#define CDPARANOIA_BASEOFFSET 0xf1d2 */
#define CDPARANOIA_BASEOFFSET 0x0

#define GST_TYPE_CDPARANOIA \
  (cdparanoia_get_type())
#define CDPARANOIA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CDPARANOIA,CDParanoia))
#define CDPARANOIA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CDPARANOIA,CDParanoiaClass))
#define GST_IS_CDPARANOIA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CDPARANOIA))
#define GST_IS_CDPARANOIA_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CDPARANOIA))

/* NOTE: per-element flags start with 16 for now */
typedef enum {
  CDPARANOIA_OPEN		= GST_ELEMENT_FLAG_LAST,

  CDPARANOIA_FLAG_LAST		= GST_ELEMENT_FLAG_LAST+2,
} CDParanoiaFlags;

typedef struct _CDParanoia CDParanoia;
typedef struct _CDParanoiaClass CDParanoiaClass;

struct _CDParanoia {
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* Index */
  GstIndex *index;
  int index_id;
  
  gchar *device;
  gchar *generic_device;
  gint default_sectors;
  gint search_overlap;
  gint endian;
  gint read_speed;
  gint toc_offset;
  gboolean toc_bias;
  gint never_skip;
  gboolean abort_on_skip;
  gint paranoia_mode;

  cdrom_drive *d;
  cdrom_paranoia *p;

  gint cur_sector;
  gint segment_start_sector;
  gint segment_end_sector;

  gint first_sector;
  gint last_sector;

  /* hacks by Gordon Irving */
  gchar discid[20];
  gint64 offsets[MAXTRK];
  gint64 total_seconds;

  gint prev_sec;
  gboolean discont_sent;
};

struct _CDParanoiaClass {
  GstElementClass parent_class;

  /* signal callbacks */
  void (*smilie_change)		(CDParanoia *cdparanoia, gchar *smilie);
  void (*transport_error)	(CDParanoia *cdparanoia, gint offset);
  void (*uncorrected_error)	(CDParanoia *cdparanoia, gint offset);
};

GType cdparanoia_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __CDPARANOIA_H__ */
