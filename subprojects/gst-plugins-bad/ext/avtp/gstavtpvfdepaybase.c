/*
 * GStreamer AVTP Plugin
 * Copyright (c) 2021, Fastree3D
 * Adrian Fiergolski <Adrian.Fiergolski@fastree3d.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include <avtp.h>

#include "gstavtpvfdepaybase.h"

GST_DEBUG_CATEGORY_STATIC (avtpvfdepaybase_debug);
#define GST_CAT_DEFAULT avtpvfdepaybase_debug

static GstStateChangeReturn gst_avtp_vf_depay_change_state (GstElement *
    element, GstStateChange transition);

#define gst_avtp_vf_depay_base_parent_class parent_class
G_DEFINE_TYPE_EXTENDED (GstAvtpVfDepayBase, gst_avtp_vf_depay_base,
    GST_TYPE_AVTP_BASE_DEPAYLOAD, G_TYPE_FLAG_ABSTRACT, {
    });

static void
gst_avtp_vf_depay_base_class_init (GstAvtpVfDepayBaseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avtp_vf_depay_change_state);

  klass->depay_push_caps = NULL;

  GST_DEBUG_CATEGORY_INIT (avtpvfdepaybase_debug, "avtpvfdepaybase",
      0, "debug category for avtpvfdepay element");
}

static void
gst_avtp_vf_depay_base_init (GstAvtpVfDepayBase * avtpvfdepaybase)
{
  avtpvfdepaybase->out_buffer = NULL;
}

static GstStateChangeReturn
gst_avtp_vf_depay_change_state (GstElement * element, GstStateChange transition)
{
  GstAvtpVfDepayBase *avtpvfdepaybase = GST_AVTP_VF_DEPAY_BASE (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    if (avtpvfdepaybase->out_buffer) {
      gst_buffer_unref (avtpvfdepaybase->out_buffer);
      avtpvfdepaybase->out_buffer = NULL;
    }
  }

  return ret;
}

GstFlowReturn
gst_avtp_vf_depay_base_push (GstAvtpVfDepayBase * avtpvfdepaybase)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpvfdepaybase);
  GstFlowReturn ret;

  if (G_UNLIKELY (!gst_pad_has_current_caps (avtpbasedepayload->srcpad))) {
    if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG) {
      GstClock *clock =
          gst_element_get_clock (GST_ELEMENT_CAST (avtpvfdepaybase));
      if (clock == NULL) {
        GST_DEBUG_OBJECT (avtpvfdepaybase,
            "Sending initial CAPS and SEGMENT, no pipeline time.");
      } else {
        GST_DEBUG_OBJECT (avtpvfdepaybase,
            "Sending initial CAPS and SEGMENT, pipeline time: %"
            GST_TIME_FORMAT, GST_TIME_ARGS (gst_clock_get_time (clock)));
        gst_object_unref (clock);
      }
    }

    g_assert (GST_AVTP_VF_DEPAY_BASE_GET_CLASS
        (avtpvfdepaybase)->depay_push_caps != NULL);
    if (!GST_AVTP_VF_DEPAY_BASE_GET_CLASS (avtpvfdepaybase)->depay_push_caps
        (avtpvfdepaybase)) {
      GST_ELEMENT_ERROR (avtpvfdepaybase, CORE, CAPS, (NULL), (NULL));
      return GST_FLOW_ERROR;
    }
  }

  ret =
      gst_avtp_base_depayload_push (GST_AVTP_BASE_DEPAYLOAD (avtpvfdepaybase),
      avtpvfdepaybase->out_buffer);
  avtpvfdepaybase->out_buffer = NULL;

  return ret;
}
