/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * validate.c - Validate generic functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_VALIDATE_INTERNAL_H__
#define __GST_VALIDATE_INTERNAL_H__

#include <gst/gst.h>
#include "gst-validate-scenario.h"
#include "gst-validate-monitor.h"
#include <json-glib/json-glib.h>

extern G_GNUC_INTERNAL GstDebugCategory *gstvalidate_debug;
#define GST_CAT_DEFAULT gstvalidate_debug

extern G_GNUC_INTERNAL GRegex *newline_regex;
extern G_GNUC_INTERNAL GstClockTime _priv_start_time;

extern G_GNUC_INTERNAL GQuark _Q_VALIDATE_MONITOR;

/* If an action type is 1 (TRUE) we also consider it is a config to keep backward compatibility */
#define IS_CONFIG_ACTION_TYPE(type) (((type) & GST_VALIDATE_ACTION_TYPE_CONFIG) || ((type) == TRUE))

extern G_GNUC_INTERNAL GType _gst_validate_action_type_type;

void init_scenarios (void);
void register_action_types (void);

/* FIXME 2.0 Remove that as this is only for backward compatibility
 * as we used to have to print actions in the action execution function
 * and this is done by the scenario itself now */
G_GNUC_INTERNAL gboolean _action_check_and_set_printed (GstValidateAction *action);
G_GNUC_INTERNAL gboolean gst_validate_action_get_level (GstValidateAction *action);
G_GNUC_INTERNAL gboolean gst_validate_scenario_check_and_set_needs_clock_sync (GList *structures, GstStructure **meta);

#define GST_VALIDATE_SCENARIO_SUFFIX ".scenario"
G_GNUC_INTERNAL gchar** gst_validate_scenario_get_include_paths(const gchar* relative_scenario);
G_GNUC_INTERNAL void _priv_validate_override_registry_deinit(void);

G_GNUC_INTERNAL GstValidateReportingDetails gst_validate_runner_get_default_reporting_details (GstValidateRunner *runner);

#define GST_VALIDATE_VALIDATE_TEST_SUFFIX ".validatetest"
G_GNUC_INTERNAL GstValidateMonitor * gst_validate_get_monitor (GObject *object);
G_GNUC_INTERNAL void gst_validate_init_runner (void);
G_GNUC_INTERNAL void gst_validate_deinit_runner (void);
G_GNUC_INTERNAL void gst_validate_report_deinit (void);
G_GNUC_INTERNAL gboolean gst_validate_send (JsonNode * root);
G_GNUC_INTERNAL void gst_validate_set_test_file_globals (GstStructure* meta, const gchar* testfile, gboolean use_fakesinks);
G_GNUC_INTERNAL gboolean gst_validate_get_test_file_scenario (GList** structs, const gchar** scenario_name, gchar** original_name);
G_GNUC_INTERNAL GstValidateScenario* gst_validate_scenario_from_structs (GstValidateRunner* runner, GstElement* pipeline, GList* structures,
    gchar* origin_file);
G_GNUC_INTERNAL GList* gst_validate_get_config (const gchar *structname);
G_GNUC_INTERNAL GList * gst_validate_get_test_file_expected_issues (void);

G_GNUC_INTERNAL gboolean gst_validate_extra_checks_init (void);
G_GNUC_INTERNAL gboolean gst_validate_flow_init (void);
G_GNUC_INTERNAL gboolean is_tty (void);

/* MediaDescriptor structures */

typedef struct _GstValidateMediaFileNode GstValidateMediaFileNode;
typedef struct _GstValidateMediaTagNode GstValidateMediaTagNode;

typedef struct
{
  /* Children */
  /* GstValidateMediaTagNode */
  GList *tags;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaTagsNode;

/* Parsing structures */
struct _GstValidateMediaFileNode
{
  /* Children */
  /* GstValidateMediaStreamNode */
  GList *streams;
  /* GstValidateMediaTagsNode */
  GstValidateMediaTagsNode *tags;

  /* attributes */
  guint64 id;
  gchar *uri;
  GstClockTime duration;
  gboolean frame_detection;
  gboolean skip_parsers;
  gboolean seekable;

  GstCaps *caps;

  gchar *str_open;
  gchar *str_close;
};

struct _GstValidateMediaTagNode
{
  /* Children */
  GstTagList *taglist;

  /* Testing infos */
  gboolean found;

  gchar *str_open;
  gchar *str_close;
};

typedef struct
{
  /* Children */
  /* GstValidateMediaFrameNode */
  GList *frames;

  /* GstValidateMediaTagsNode */
  GstValidateMediaTagsNode *tags;

  /* Attributes */
  GstCaps *caps;
  GList * segments;
  gchar *id;
  gchar *padname;

  /* Testing infos */
  GstPad *pad;
  GList *cframe;

  gchar *str_open;
  gchar *str_close;
} GstValidateMediaStreamNode;

typedef struct
{
  /* Attributes */
  guint64 id;
  guint64 offset;
  guint64 offset_end;
  GstClockTime duration;
  GstClockTime pts, dts;
  GstClockTime running_time;
  gboolean is_keyframe;

  GstBuffer *buf;

  gchar *checksum;
  gchar *str_open;
  gchar *str_close;
} GstValidateMediaFrameNode;

typedef struct
{
  gint next_frame_id;

  GstSegment segment;

  gchar *str_open;
  gchar *str_close;
} GstValidateSegmentNode;

void gst_validate_filenode_free (GstValidateMediaFileNode *
    filenode);
gboolean gst_validate_tag_node_compare (GstValidateMediaTagNode *
    tnode, const GstTagList * tlist);

GstValidateMediaFileNode * gst_validate_media_descriptor_get_file_node (GstValidateMediaDescriptor *self);

#endif
