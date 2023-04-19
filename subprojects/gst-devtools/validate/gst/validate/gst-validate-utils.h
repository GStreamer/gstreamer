/* GStreamer
 *
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-utils.h - Some utility functions
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

#ifndef EXPRESSION_PARSER_H
#define EXPRESSION_PARSER_H

#include<setjmp.h>
#include<stdlib.h>
#include<glib.h>
#include<gio/gio.h>
#include <gst/gst.h>
#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"

typedef int (*GstValidateParseVariableFunc) (const gchar *name,
    double *value, gpointer user_data);

typedef gchar** (*GstValidateGetIncludePathsFunc)(const gchar* includer_file);

GST_VALIDATE_API
gdouble gst_validate_utils_parse_expression (const gchar *expr,
                                             GstValidateParseVariableFunc variable_func,
                                             gpointer user_data,
                                             gchar **error);
GST_VALIDATE_API
guint gst_validate_utils_flags_from_str     (GType type, const gchar * str_flags);
GST_VALIDATE_API
gboolean gst_validate_utils_enum_from_str   (GType type,
                                             const gchar * str_enum,
                                             guint * enum_value);
GST_VALIDATE_API
gchar ** gst_validate_utils_get_strv       (GstStructure *str, const gchar *fieldname);

GST_VALIDATE_API
GList * gst_validate_utils_structs_parse_from_filename         (const gchar * scenario_file,
                                                                GstValidateGetIncludePathsFunc get_include_paths_func,
                                                                gchar **file_path);
GST_VALIDATE_API
GstStructure * gst_validate_utils_test_file_get_meta           (const gchar * testfile, gboolean use_fakesinks);

GST_VALIDATE_API
GList * gst_validate_structs_parse_from_gfile                  (GFile * scenario_file,
                                                                GstValidateGetIncludePathsFunc get_include_paths_func);

GST_VALIDATE_API
gboolean gst_validate_element_has_klass (GstElement * element, const gchar * klass);
GST_VALIDATE_API
gboolean gst_validate_utils_get_clocktime (GstStructure *structure, const gchar * name,
        GstClockTime * retval);

GST_VALIDATE_API
GstValidateActionReturn gst_validate_object_set_property (GstValidateReporter * reporter,
                                                          GObject * object,
                                                          const gchar * property,
                                                          const GValue * value,
                                                          gboolean optional);

GST_VALIDATE_API
GstValidateActionReturn gst_validate_object_set_property_full(GstValidateReporter * reporter,
                                                              GObject * object,
                                                              const gchar * property,
                                                              const GValue * value,
                                                              GstValidateObjectSetPropertyFlags flags);


GST_VALIDATE_API
void gst_validate_spin_on_fault_signals (void);

GST_VALIDATE_API
gboolean gst_validate_element_matches_target (GstElement * element, GstStructure * s);
gchar * gst_validate_replace_variables_in_string (gpointer incom, GstStructure * local_vars, const gchar * in_string,
    GstValidateStructureResolveVariablesFlags flags);
GST_VALIDATE_API
void gst_validate_structure_resolve_variables (gpointer source, GstStructure *structure, GstStructure *local_variables,
    GstValidateStructureResolveVariablesFlags flags);
void gst_validate_structure_set_variables_from_struct_file(GstStructure* vars, const gchar* struct_file);
void gst_validate_set_globals(GstStructure* structure);
GST_VALIDATE_API
gboolean gst_validate_fail_on_missing_plugin(void);
GST_VALIDATE_API gboolean gst_validate_has_colored_output(void);

#endif
