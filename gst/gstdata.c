#include "gstdata.h"
/* debugging and error checking */
#include "gstlog.h"
#include "gstinfo.h"

static void			gst_data_destroy		(GstData *data);
static void			gst_data_free			(GstData *data);

void
gst_data_init (GstData *data)
{
  guint i;
  
  data->dispose = gst_data_dispose;
  data->free = gst_data_free;
#ifdef HAVE_ATOMIC_H
  atomic_set (&data->refcount, 1);
#else
  data->refcount = 1;
  data->reflock = g_mutex_new();
#endif
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    data->offset[i] = 0;
  }
}
void
gst_data_copy (GstData *to, const GstData *from)
{
  guint i;
  
  to->type = from->type;
  to->dispose = from->dispose;
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    to->offset[i] = from->offset[i];
  }
}
void
gst_data_dispose (GstData *data)
{
#ifdef HAVE_ATOMIC_H
#else
  g_mutex_free (data->reflock);
#endif
}
static void
gst_data_free (GstData *data)
{
  g_free (data);
}
static void
gst_data_destroy (GstData *data)
{
  GST_DEBUG (GST_CAT_BUFFER, "destroying %p (type %d)\n", data, data->type);
  data->dispose (data);
  data->free (data);
}
void
gst_data_ref (GstData* data)
{
  g_return_if_fail (data != NULL);

#ifdef HAVE_ATOMIC_H
  GST_DEBUG (GST_CAT_BUFFER, "ref data %p, current count is %d", data, atomic_read (&data->refcount));
  g_return_if_fail (atomic_read (&data->refcount) > 0);
  atomic_inc (&data->refcount);
#else
  GST_INFO (GST_CAT_BUFFER, "ref data %p, current count is %d", data, data->refcount);
  g_return_if_fail (data->refcount > 0);
  g_mutex_lock (data->reflock);
  buffer->refcount++;
  g_mutex_unlock (data->reflock);
#endif
}
void
gst_data_unref (GstData* data)
{
  gint destroy;

  g_return_if_fail (data != NULL);

#ifdef HAVE_ATOMIC_H
  GST_DEBUG (GST_CAT_BUFFER, "unref data %p (type %d), current count is %d\n", data, data->type, atomic_read (&data->refcount));
  g_return_if_fail (atomic_read (&data->refcount) > 0);
  destroy = atomic_dec_and_test (&data->refcount);
#else
  GST_DEBUG (GST_CAT_BUFFER, "unref data %p (type %d), current count is %d\n", data, data->type, atomic_read (&data->refcount));
  g_return_if_fail (data->refcount > 0);
  g_mutex_lock (data->reflock);
  data->refcount--;
  destroy = (buffer->refcount == 0);
  g_mutex_lock (data->reflock);
#endif

  /* if we ended up with the refcount at zero, destroy the buffer */
  if (destroy) {
    gst_data_destroy (data);
  }
}  
  