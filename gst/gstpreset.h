/* GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 *
 * gstpreset.h: helper interface header for element presets
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

#ifndef __GST_PRESET_H__
#define __GST_PRESET_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>

G_BEGIN_DECLS

#define GST_TYPE_PRESET               (gst_preset_get_type())
#define GST_PRESET(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PRESET, GstPreset))
#define GST_IS_PRESET(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PRESET))
#define GST_PRESET_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_PRESET, GstPresetInterface))


typedef struct _GstPreset GstPreset; /* dummy object */
typedef struct _GstPresetInterface GstPresetInterface;

struct _GstPresetInterface
{
  GTypeInterface parent;

  GList* (*get_preset_names) (GstPreset *self);

  gboolean (*load_preset) (GstPreset *self, const gchar *name);
  gboolean (*save_preset) (GstPreset *self, const gchar *name);
  gboolean (*rename_preset) (GstPreset *self, const gchar *old_name, const gchar *new_name);
  gboolean (*delete_preset) (GstPreset *self, const gchar *name);
  
  gboolean (*set_meta) (GstPreset *self,const gchar *name, const gchar *tag, gchar *value);
  gboolean (*get_meta) (GstPreset *self,const gchar *name, const gchar *tag, gchar **value);

  void (*create_preset) (GstPreset *self);
  
  /* @todo:
   *
   * need a presets-changed signal, to notify of changes in preset list
   *
   * need a way to sync class instances, we want to keep only one list for all
   * instances of a type and if the list changes, we trigger the signal for all
   * instance
   */

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_preset_get_type(void);

GList* gst_preset_get_preset_names (GstPreset *self);

gboolean gst_preset_load_preset (GstPreset *self, const gchar *name);
gboolean gst_preset_save_preset (GstPreset *self, const gchar *name);
gboolean gst_preset_rename_preset (GstPreset *self, const gchar *old_name, const gchar *new_name);
gboolean gst_preset_delete_preset (GstPreset *self, const gchar *name);

gboolean gst_preset_set_meta (GstPreset *self,const gchar *name, const gchar *tag, gchar *value);
gboolean gst_preset_get_meta (GstPreset *self,const gchar *name, const gchar *tag, gchar **value);

void gst_preset_create_preset (GstPreset *self);

G_END_DECLS

#endif /* __GST_PRESET_H__ */
