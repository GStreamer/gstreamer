/*
 *  gstvaapiutils_tsb.c - Timestamp buffer store
 *
 *  Copyright (C) 2011 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapiutils_tsb.h"
#include <glib.h>

typedef struct _GstVaapiTSBEntry        GstVaapiTSBEntry;

struct _GstVaapiTSB {
    GList              *list;
};

struct _GstVaapiTSBEntry {
    GstBuffer          *buffer;
    guint32             buffer_size;
    guint32             offset;
};

static GstVaapiTSBEntry *
gst_vaapi_tsb_entry_new(GstBuffer *buffer)
{
    GstVaapiTSBEntry *e;

    e = g_slice_new(GstVaapiTSBEntry);
    if (!e)
        return NULL;

    e->buffer      = gst_buffer_ref(buffer);
    e->buffer_size = GST_BUFFER_SIZE(buffer);
    e->offset      = 0;
    return e;
}

static void
gst_vaapi_tsb_entry_destroy(GstVaapiTSBEntry *e)
{
    if (!e)
        return;

    if (e->buffer) {
        gst_buffer_unref(e->buffer);
        e->buffer = NULL;
    }
    g_slice_free(GstVaapiTSBEntry, e);
}

/**
 * gst_vaapi_tsb_new:
 *
 * Creates a new #GstVaapiTSB.
 *
 * Return value: a new #GstVaapiTSB
 */
GstVaapiTSB *
gst_vaapi_tsb_new()
{
    GstVaapiTSB *tsb;

    tsb = g_new(GstVaapiTSB, 1);
    if (!tsb)
        return NULL;

    tsb->list = NULL;
    return tsb;
}

/**
 * gst_vaapi_tsb_destroy:
 * @tsb: a #GstVaapiTSB
 *
 * Destroys the #GstVaapiTSB. All buffers are unreferenced.
 */
void
gst_vaapi_tsb_destroy(GstVaapiTSB *tsb)
{
    if (!tsb)
        return;

    if (tsb->list) {
        g_list_foreach(tsb->list, (GFunc)gst_vaapi_tsb_entry_destroy, NULL);
        g_list_free(tsb->list);
        tsb->list = NULL;
    }
    g_free(tsb);
}

/**
 * gst_vaapi_tsb_push:
 * @tsb: a #GstVaapiTSB
 * @buffer: a #GstBuffer
 *
 * Pushes @buffer to the timestamp buffer store. The TSB owns and
 * maintains an extra reference to the buffer.
 *
 * Return value: %TRUE if success, %FALSE otherwise
 */
gboolean
gst_vaapi_tsb_push(GstVaapiTSB *tsb, GstBuffer *buffer)
{
    GList *l;
    GstVaapiTSBEntry *e;

    if (!tsb)
        return FALSE;

    e = gst_vaapi_tsb_entry_new(buffer);
    if (!e)
        return FALSE;

    l = g_list_append(tsb->list, e);
    if (!l)
        return FALSE;

    tsb->list = l;
    return TRUE;
}

/**
 * gst_vaapi_tsb_pop:
 * @tsb: a #GstVaapiTSB
 * @size: number of bytes to remove from the TSB
 *
 * Removes @size bytes from the @tsb.
 */
void
gst_vaapi_tsb_pop(GstVaapiTSB *tsb, gsize size)
{
    GList *l;
    GstVaapiTSBEntry *e;
    guint32 n;

    if (!tsb || !tsb->list)
        return;

    l = tsb->list;
    e = l->data;
    while (size > 0) {
        n = MIN(e->buffer_size - e->offset, size);
        e->offset += n;
        size -= n;
        if (e->offset == e->buffer_size) {
            gst_vaapi_tsb_entry_destroy(e);
            l = l->next;
            g_list_free_1(tsb->list);
            tsb->list = l;
            if (!l)
                return;
            e = l->data;
        }
    }
}

/**
 * gst_vaapi_tsb_peek:
 * @tsb: a #GstVaapiTSB
 *
 * Returns the current #GstBuffer.
 *
 * Return value: current #GstBuffer, or %NULL if none was found
 */
GstBuffer *
gst_vaapi_tsb_peek(GstVaapiTSB *tsb)
{
    GstVaapiTSBEntry *e;

    if (!tsb || !tsb->list)
        return NULL;

    e = tsb->list->data;
    if (!e)
        return NULL;

    return e->buffer;
}

/**
 * gst_vaapi_tsb_get_timestamp:
 * @tsb: a #GstVaapiTSB
 *
 * Returns the timestamp for the current #GstBuffer.
 *
 * Return value: current #GstBuffer timestamp, or %GST_CLOCK_TIME_NONE if none was found
 */
GstClockTime
gst_vaapi_tsb_get_timestamp(GstVaapiTSB *tsb)
{
    GstBuffer *buffer;

    buffer = gst_vaapi_tsb_peek(tsb);
    if (!buffer)
        return GST_CLOCK_TIME_NONE;

    return GST_BUFFER_TIMESTAMP(buffer);
}

/**
 * gst_vaapi_tsb_get_size:
 * @tsb: a #GstVaapiTSB
 *
 * Returns the size of the #GstVaapiTSB.
 *
 * Return value: how many bytes left to consume from @tsb
 */
gsize
gst_vaapi_tsb_get_size(GstVaapiTSB *tsb)
{
    GList *l;
    GstVaapiTSBEntry *e;
    guint32 size = 0;

    if (!tsb || !tsb->list)
        return 0;

    for (l = tsb->list; l != NULL; l = l->next) {
        e = l->data;
        size += e->buffer_size - e->offset;
    }
    return size;
}
