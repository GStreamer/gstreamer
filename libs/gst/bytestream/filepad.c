/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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

#include "filepad.h"
#include <string.h>             /* memcpy */

#define RETURN_IF_FAIL(pad,check,error) G_STMT_START { \
  if (!(check)) { \
    GST_LOG_OBJECT (pad, "setting error to %d: " #error, error); \
    pad->error_number = error; \
    return -error; \
  } \
} G_STMT_END

GST_DEBUG_CATEGORY_STATIC (gst_file_pad_debug);
#define GST_CAT_DEFAULT gst_file_pad_debug

#define _do_init(thing) \
  GST_DEBUG_CATEGORY_INIT (gst_file_pad_debug, "GstFilePad", 0, "object to splice and merge buffers to dewsired size")
GST_BOILERPLATE_FULL (GstFilePad, gst_file_pad, GstRealPad, GST_TYPE_REAL_PAD,
    _do_init)

     static void gst_file_pad_dispose (GObject * object);
     static void gst_file_pad_finalize (GObject * object);

     static void gst_file_pad_chain (GstPad * pad, GstData * data);
     static void gst_file_pad_parent_set (GstObject * object,
    GstObject * parent);


     static void gst_file_pad_base_init (gpointer g_class)
{
}

static void
gst_file_pad_class_init (GstFilePadClass * klass)
{
  GstObjectClass *gst = GST_OBJECT_CLASS (klass);
  GObjectClass *object = G_OBJECT_CLASS (klass);

  object->dispose = gst_file_pad_dispose;
  object->finalize = gst_file_pad_finalize;

  gst->parent_set = gst_file_pad_parent_set;
}

static void
gst_file_pad_init (GstFilePad * pad)
{
  GstRealPad *real = GST_REAL_PAD (pad);

  /* must do this for set_chain_function to work */
  real->direction = GST_PAD_SINK;

  gst_pad_set_chain_function (GST_PAD (real), gst_file_pad_chain);

  pad->adapter = gst_adapter_new ();
  pad->in_seek = FALSE;
  pad->eos = FALSE;

  pad->iterate_func = NULL;
  pad->event_func = gst_pad_event_default;
}

static void
gst_file_pad_dispose (GObject * object)
{
  GstFilePad *file_pad = GST_FILE_PAD (object);

  gst_adapter_clear (file_pad->adapter);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_file_pad_finalize (GObject * object)
{
  GstFilePad *file_pad = GST_FILE_PAD (object);

  g_object_unref (file_pad->adapter);

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_file_pad_chain (GstPad * gst_pad, GstData * data)
{
  GstFilePad *pad = GST_FILE_PAD (gst_pad);
  gboolean got_value;
  gint64 value;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    /* INSERT ME */
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        got_value =
            gst_event_discont_get_value (event, GST_FORMAT_BYTES, &value);
        if (!got_value)
          got_value =
              gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &value);
        if (pad->in_seek) {
          /* discard broken disconts */
          if ((pad->position >= 0) && got_value && (value != pad->position)) {
            GST_DEBUG_OBJECT (pad,
                "unexpected discont during seek (want %" G_GINT64_FORMAT
                ", got %" G_GINT64_FORMAT "), discarding", pad->position,
                value);
            break;
          }
          if (got_value) {
            GST_INFO_OBJECT (pad, "got discont to %" G_GINT64_FORMAT, value);
            pad->position = value;
          } else {
            GST_WARNING_OBJECT (pad, "got discont without position");
            if (pad->position == -1) {
              GST_WARNING_OBJECT (pad,
                  "need to reset position to 0 because we have no position info");
              pad->position = 0;
            }
          }
          pad->in_seek = FALSE;
        } else {
          /* we're not seeking, what does the event want from us? */
          if (!got_value ||
              value != pad->position + gst_adapter_available (pad->adapter)) {
            /* discont doesn't match position */
            GST_WARNING_OBJECT (pad, "DISCONT arrived to %" G_GINT64_FORMAT
                ", we're expecting %" G_GINT64_FORMAT " though", value,
                pad->position + gst_adapter_available (pad->adapter));
            /* off to event function, let the user decide */
            break;
          }
        }
        gst_event_unref (event);
        return;
      case GST_EVENT_EOS:
        pad->eos = TRUE;
        gst_event_unref (event);
        g_return_if_fail (pad->iterate_func);
        pad->iterate_func (pad);
        return;
      default:
        break;
    }

    g_return_if_fail (pad->event_func);
    pad->event_func (gst_pad, event);
  } else {
    if (pad->in_seek) {
      GST_DEBUG_OBJECT (pad, "discarding buffer %p, we're seeking", data);
      gst_data_unref (data);
    } else {
      gst_adapter_push (pad->adapter, GST_BUFFER (data));
      g_return_if_fail (pad->iterate_func);
      pad->iterate_func (pad);
    }
  }
}

static void
gst_file_pad_parent_set (GstObject * object, GstObject * parent)
{
  GstElement *element;

  /* FIXME: we can only be added to elements, right? */
  element = GST_ELEMENT (parent);

  if (element->loopfunc)
    g_warning ("attempt to add a GstFilePad to a loopbased element.");
  if (!GST_FLAG_IS_SET (element, GST_ELEMENT_EVENT_AWARE))
    g_warning ("elements using GstFilePad must be event-aware.");

  GST_CALL_PARENT (GST_OBJECT_CLASS, parent_set, (object, parent));
}

/**
 * gst_file_pad_new:
 * @templ: the #GstPadTemplate to use
 * @name: name of the pad
 *
 * creates a new file pad. Note that the template must be a sink template.
 *
 * Returns: the new file pad.
 */
GstPad *
gst_file_pad_new (GstPadTemplate * templ, gchar * name)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);
  g_return_val_if_fail (GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SINK,
      NULL);
  g_return_val_if_fail (name != NULL, NULL);

  return gst_pad_custom_new_from_template (GST_TYPE_FILE_PAD, templ, name);
}

/**
 * gst_file_pad_set_event_function:
 * @pad: a #GstFilePad
 * @event: the #GstPadEventFunction to use
 *
 * sets the function to use for handling events arriving on the pad, that aren't 
 * intercepted by the pad. The pad intercepts EOS and DISCONT events for its own
 * usage. An exception are unexpected DISCONT events that signal discontinuities 
 * in the data stream. So when your event function receives a DISCONT event, it has
 * to decide what to do with a hole of data coming up.
 * If you don't set one, gst_pad_event_default() will be used.
 */
void
gst_file_pad_set_event_function (GstFilePad * pad, GstPadEventFunction event)
{
  g_return_if_fail (GST_IS_FILE_PAD (pad));
  g_return_if_fail (event != NULL);

  pad->event_func = event;
}

/**
 * gst_file_pad_set_iterate_function:
 * @pad: a #GstFilePad
 * @iterate: the #GstFilePadIterateFunction to use
 *
 * Sets the iterate function of the pad. Don't use chain functions with file 
 * pads, they are used internally. The iteration function is called whenever 
 * there is new data to process. You can then use operations like 
 * gst_file_pad_read() to get the data.
 */
void
gst_file_pad_set_iterate_function (GstFilePad * pad,
    GstFilePadIterateFunction iterate)
{
  g_return_if_fail (GST_IS_FILE_PAD (pad));
  g_return_if_fail (iterate != NULL);

  pad->iterate_func = iterate;
}

/**
 * gst_file_pad_read:
 * @pad: a #GstFilePad
 * @buf: buffer to fill
 * @count: number of bytes to put into buffer
 *
 * read @count bytes from the @pad into the buffer starting at @buf.
 * Note that this function does not return less bytes even in the EOS case.
 *
 * Returns: On success, @count is returned, and the file position is 
 *	    advanced by this number. 0 indicates end of stream.
 *	    On error, -errno is returned, and the errno of the pad is set 
 *	    appropriately. In this case the file position will not change.
 */
gint64
gst_file_pad_read (GstFilePad * pad, void *buf, gint64 count)
{
  const guint8 *data;

  /* FIXME: set pad's errno? */
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), -EBADF);
  g_return_val_if_fail (buf != NULL, -EFAULT);
  g_return_val_if_fail (count >= 0, -EINVAL);

  if (gst_file_pad_eof (pad))
    return 0;
  data = gst_adapter_peek (pad->adapter, count);
  RETURN_IF_FAIL (pad, data != NULL, EAGAIN);

  memcpy (buf, data, count);
  gst_adapter_flush (pad->adapter, count);
  pad->position += count;

  return count;
}

/**
 * gst_file_pad_try_read:
 * @pad: a #GstFilePad
 * @buf: buffer to fill
 * @count: number of bytes to put into buffer
 *
 * Attempts to read up to @count bytes into the buffer pointed to by @buf.
 * This function is modeled after the libc read() function.
 *
 * Returns: On success, the number of bytes read is returned, and the file 
 *	    position is advanced by this number. 0 indicates end of stream.
 *	    On error, -errno is returned, and the errno of the pad is set 
 *	    appropriately. In this case the file position will not change.
 *	    Note that the number of bytes read may and often will be 
 *	    smaller then @count. If you don't want this behaviour, use 
 *	    gst_file_pad_read() instead.
 **/
gint64
gst_file_pad_try_read (GstFilePad * pad, void *buf, gint64 count)
{
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), -EBADF);
  g_return_val_if_fail (buf != NULL, -EFAULT);
  g_return_val_if_fail (count >= 0, -EINVAL);

  count = MIN (count, (gint64) gst_adapter_available (pad->adapter));
  return gst_file_pad_read (pad, buf, count);
}

/**
 * gst_file_pad_seek:
 * @pad: a #GstFilePad
 * @offset: new offset to start reading from
 * @whence: specifies relative to which position @offset should be interpreted
 *
 * Sets the new position to read from to @offset bytes relative to @whence. This 
 * function is modelled after the fseek() libc function.
 *
 * Returns: 0 on success, a negative error number on failure.
 */
int
gst_file_pad_seek (GstFilePad * pad, gint64 offset, GstSeekType whence)
{
  GstEvent *event;

  g_return_val_if_fail (GST_IS_FILE_PAD (pad), -EBADF);
  g_return_val_if_fail ((whence & (GST_SEEK_METHOD_CUR | GST_SEEK_METHOD_SET |
              GST_SEEK_METHOD_END)) == whence, -EINVAL);
  g_return_val_if_fail (whence != 0, -EINVAL);

  RETURN_IF_FAIL (pad, GST_PAD_PEER (pad), EBADF);      /* FIXME: better return val? */
  /* adjust offset by number of bytes buffered */
  if (whence & GST_SEEK_METHOD_CUR)
    offset -= gst_adapter_available (pad->adapter);
  event =
      gst_event_new_seek (whence | GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE
      | GST_FORMAT_BYTES, offset);
  RETURN_IF_FAIL (pad, gst_pad_send_event (GST_PAD_PEER (pad), event), EBADF);
  GST_DEBUG_OBJECT (pad,
      "seeking to position %" G_GINT64_FORMAT " relative to %s", offset,
      whence == GST_SEEK_METHOD_CUR ? "start" : whence ==
      GST_SEEK_METHOD_SET ? "current" : "end");
  switch (whence) {
    case GST_SEEK_METHOD_SET:
      pad->position = offset;
      break;
    case GST_SEEK_METHOD_CUR:
      pad->position += offset + gst_adapter_available (pad->adapter);
      break;
    case GST_SEEK_METHOD_END:
      /* FIXME: query length and use that */
      pad->position = -1;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  gst_adapter_clear (pad->adapter);
  pad->in_seek = TRUE;
  pad->eos = FALSE;

  return 0;
}

/**
 * gst_file_pad_tell:
 * @pad: a #GstFilePad
 *
 * gets the current position inside the stream if the position is available. If
 * the position is not available due to a pending seek, it returns -EAGAIN. If 
 * the stream does not provide this information, -EBADF is returned. The @pad's 
 * errno is set apropriatly. This function is modeled after the ftell() libc 
 * function.
 *
 * Returns: The position or a negative error code.
 */
gint64
gst_file_pad_tell (GstFilePad * pad)
{
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), -EBADF);

  RETURN_IF_FAIL (pad, !(pad->position < 0 && pad->in_seek), EAGAIN);
  RETURN_IF_FAIL (pad, pad->position >= 0, EBADF);

  return pad->position;
}

/**
 * gst_file_pad_error:
 * @pad: a #GstFilePad
 *
 * Gets the last error. This is modeled after the ferror() function from libc.
 *
 * Returns: Number of the last error or 0 if there hasn't been an error yet.
 */
int
gst_file_pad_error (GstFilePad * pad)
{
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), 0);

  return pad->error_number;
}

/**
 * gst_file_pad_eof:
 * @pad: a #GstFilePad
 *
 * Checks if the EOS has been reached. This function is modeled after the 
 * function feof() from libc.
 *
 * Returns: non-zero if EOS has been reached, zero if not.
 **/
int
gst_file_pad_eof (GstFilePad * pad)
{
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), 0);

  if (pad->in_seek)
    return 0;
  if (gst_adapter_available (pad->adapter))
    return 0;
  if (!pad->eos)
    return 0;

  return 1;
}

/**
 * gst_file_pad_available:
 * @pad: a #GstFilePad
 *
 * Use this function to figure out the maximum number of bytes that can be read
 * via gst_file_pad_read() without that function returning -EAGAIN.
 * 
 * Returns: the number of bytes available in the file pad.
 */
guint
gst_file_pad_available (GstFilePad * pad)
{
  g_return_val_if_fail (GST_IS_FILE_PAD (pad), 0);

  return gst_adapter_available (pad->adapter);
}

/**
 * gst_file_pad_get_length:
 * @pad: a #GstFilePad
 *
 * Gets the length in bytes of the @pad.
 *
 * Returns: length in bytes or -1 if not available
 */
gint64
gst_file_pad_get_length (GstFilePad * pad)
{
  GstFormat format = GST_FORMAT_BYTES;
  gint64 length;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_FILE_PAD (pad), -1);

  /* we query the length every time to avoid issues with changing lengths */
  peer = GST_PAD_PEER (pad);
  if (!peer)
    return -1;
  if (gst_pad_query (peer, GST_QUERY_TOTAL, &format, &length))
    return length;
  format = GST_FORMAT_DEFAULT;
  if (gst_pad_query (peer, GST_QUERY_TOTAL, &format, &length))
    return length;

  return -1;
}
