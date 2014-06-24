/* GStreamer
 * Copyright (C) 2001 Wim Taymans <wim.taymans@chello.be>
 *               2004 Edward Hervey <bilboed@bilboed.com>
 *
 * gnloperation.h: Header for base GnlOperation
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


#ifndef __GNL_OPERATION_H__
#define __GNL_OPERATION_H__

#include <gst/gst.h>
#include "gnlobject.h"

G_BEGIN_DECLS
#define GNL_TYPE_OPERATION \
  (gnl_operation_get_type())
#define GNL_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GNL_TYPE_OPERATION,GnlOperation))
#define GNL_OPERATION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GNL_TYPE_OPERATION,GnlOperationClass))
#define GNL_IS_OPERATION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GNL_TYPE_OPERATION))
#define GNL_IS_OPERATION_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GNL_TYPE_OPERATION))
    struct _GnlOperation
{
  GnlObject parent;

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

struct _GnlOperationClass
{
  GnlObjectClass parent_class;

  void	(*input_priority_changed) (GnlOperation * operation, GstPad *pad, guint32 priority);
};

GstPad * get_unlinked_sink_ghost_pad (GnlOperation * operation);

void
gnl_operation_signal_input_priority_changed(GnlOperation * operation, GstPad *pad,
					    guint32 priority);

void gnl_operation_update_base_time (GnlOperation *operation,
                                     GstClockTime timestamp);


/* normal GOperation stuff */
GType gnl_operation_get_type (void);

G_END_DECLS
#endif /* __GNL_OPERATION_H__ */
