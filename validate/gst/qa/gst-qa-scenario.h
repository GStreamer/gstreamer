/* GStreamer
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-qa-runner.c - QA Runner class
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

#ifndef __GST_QA_SCENARIO_H__
#define __GST_QA_SCENARIO_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_QA_SCENARIO            (gst_qa_scenario_get_type ())
#define GST_QA_SCENARIO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_SCENARIO, GstQaScenario))
#define GST_QA_SCENARIO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_SCENARIO, GstQaScenarioClass))
#define GST_IS_QA_SCENARIO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_SCENARIO))
#define GST_IS_QA_SCENARIO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_SCENARIO))
#define GST_QA_SCENARIO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_SCENARIO, GstQaScenarioClass))

typedef struct _GstQaScenario      GstQaScenario;
typedef struct _GstQaScenarioClass GstQaScenarioClass;
typedef struct _GstQaScenarioPrivate GstQaScenarioPrivate;


struct _GstQaScenarioClass
{
  GObjectClass parent_class;

  GMarkupParser content_parser;
};

struct _GstQaScenario
{
  GObject parent;

  GstQaScenarioPrivate *priv;
};

GType gst_qa_scenario_get_type (void);

GstQaScenario * gst_qa_scenario_factory_create (GstElement *pipeline,
                                                const gchar *scenario_name);

G_END_DECLS

#endif /* __GST_QA_SCENARIOS__ */
