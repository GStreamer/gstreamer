/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <bilboed@bilboed.com>
 *
 * nleoperation.h: Header for base NleOperation
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
 */


#ifndef __NLE_OPERATION_H__
#define __NLE_OPERATION_H__

#include <gst/gst.h>
#include "nleobject.h"

G_BEGIN_DECLS
#define NLE_TYPE_OPERATION \
  (nle_operation_get_type())
#define NLE_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),NLE_TYPE_OPERATION,NleOperation))
#define NLE_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),NLE_TYPE_OPERATION,NleOperationClass))
#define NLE_IS_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),NLE_TYPE_OPERATION))
#define NLE_IS_OPERATION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),NLE_TYPE_OPERATION))
    struct _NleOperation
{
  NleObject parent;

  /* <private> */

  /* num_sinks:
   * Number of sink inputs of the controlled element.
   * -1 if the sink pads are dynamic */
  gint num_sinks;

  /* TRUE if element has request pads */
  gboolean dynamicsinks;

  /* realsinks:
   * Number of sink pads currently used on the contolled element. */
  gint realsinks;

  /* FIXME : We might need to use a lock to access this list */
  GList * sinks;		/* The sink ghostpads */
  
  GstElement *element;		/* controlled element */

  GstClockTime next_base_time;
};

struct _NleOperationClass
{
  NleObjectClass parent_class;

  void	(*input_priority_changed) (NleOperation * operation, GstPad *pad, guint32 priority);
};

GstPad * get_unlinked_sink_ghost_pad (NleOperation * operation);

void
nle_operation_signal_input_priority_changed(NleOperation * operation, GstPad *pad,
					    guint32 priority);

void nle_operation_update_base_time (NleOperation *operation,
                                     GstClockTime timestamp);

void nle_operation_hard_cleanup (NleOperation *operation);


/* normal GOperation stuff */
GType nle_operation_get_type (void);

G_END_DECLS
#endif /* __NLE_OPERATION_H__ */
