/* Non Linear Engine plugin
 *
 * Copyright (C) 2023 Thibault Saunier <tsaunier@igalia.com>
 *
 * validate.c
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>
#include "nle.h"

GST_DEBUG_CATEGORY_STATIC (nle_validate_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT nle_validate_debug

void nle_validate_init (void);

#ifdef G_HAVE_ISO_VARARGS
#define REPORT_UNLESS(condition, errpoint, ...)                                \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR,              \
                                 __VA_ARGS__);                                 \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
#ifdef G_HAVE_GNUC_VARARGS
#define REPORT_UNLESS(condition, errpoint, args...)                            \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR, ##args);     \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#endif /* G_HAVE_ISO_VARARGS */
#endif /* G_HAVE_GNUC_VARARGS */

#define NLE_START_VALIDATE_ACTION(funcname)                                    \
static gint                                                                    \
funcname(GstValidateScenario *scenario, GstValidateAction *action) {           \
  GstValidateActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;                \
  GstElement * pipeline = gst_validate_scenario_get_pipeline(scenario);

#define NLE_END_VALIDATE_ACTION                                                \
done:                                                                          \
  gst_clear_object(&pipeline);                                                 \
  return res;                                                                  \
}

NLE_START_VALIDATE_ACTION (_add_object)
{
  GError *err = NULL;
  GstElement *nleobj = NULL;
  GstElement *child =
      gst_parse_bin_from_description_full (gst_structure_get_string
      (action->structure, "desc"), FALSE, NULL,
      GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS | GST_PARSE_FLAG_PLACE_IN_BIN,
      &err);
  const gchar *objname = gst_structure_get_string (action->structure,
      "object-name");

  REPORT_UNLESS (child, clean, "Failed to create element from description: %s",
      err ? err->message : "Unknown error");

  nleobj = nle_find_object_in_bin_recurse (GST_BIN (pipeline), objname);

  REPORT_UNLESS (nleobj, clean, "Could not find object `%s`", objname);

  gboolean is_operation = NLE_IS_OPERATION (nleobj);
  gboolean is_src = NLE_IS_SOURCE (nleobj);
  if (GST_IS_BIN (child) && (is_src || is_operation)) {
    if (child->numsrcpads == 0 && !gst_element_class_get_pad_template
        (GST_ELEMENT_GET_CLASS (child), "src")) {
      GstPad *srcpad = gst_bin_find_unlinked_pad (GST_BIN (child), GST_PAD_SRC);
      if (srcpad) {
        gst_element_add_pad (child, gst_ghost_pad_new ("src", srcpad));
        gst_object_unref (srcpad);
      }
    }

    if (is_operation && child->numsinkpads == 0
        && !gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (child),
            "sink")) {
      GstPad *sinkpad =
          gst_bin_find_unlinked_pad (GST_BIN (child), GST_PAD_SINK);
      if (sinkpad) {
        gst_element_add_pad (child, gst_ghost_pad_new ("sink", sinkpad));
        gst_object_unref (sinkpad);
      }
    }

  }
  REPORT_UNLESS (gst_bin_add (GST_BIN (nleobj), gst_object_ref (child)), clean,
      "Could not add child to nle object");

clean:
  g_clear_error (&err);
  gst_clear_object (&child);

  gst_clear_object (&nleobj);

  goto done;
}

NLE_END_VALIDATE_ACTION;

static void
register_action_types ()
{
  GST_DEBUG_CATEGORY_INIT (nle_validate_debug, "nlevalidate",
      GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "NLE validate");

/* *INDENT-OFF* */
  gst_validate_register_action_type ("nle-add-child", "nle",
    _add_object,
      (GstValidateActionParameter [])  {
        {
         .name = "object-name",
         .description = "The name of the nle object to which to add child, will recurse, \n"
         " potentially in `nlecompositions` to find the right object",
         .mandatory = TRUE,
         .types = "string",
        },
        {
         .name = "desc",
         .description = "The 'bin description' of the child to add",
         .mandatory = TRUE,
         .types = "string",
        },
        {NULL}
       },
       "Add a child to a NleObject\n",
       GST_VALIDATE_ACTION_TYPE_NONE);
/* *INDENT-ON* */
}


void
nle_validate_init ()
{
  register_action_types ();
}
