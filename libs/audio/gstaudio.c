#include "gstaudio.h"

double 
gst_audio_length (GstPad* pad, GstBuffer* buf)
{
/* calculate length in seconds
 * of audio buffer buf
 * based on capabilities of pad
 */

  long bytes = 0;
  int width = 0;
  int channels = 0;
  long rate = 0L;

  double length;

  GstCaps *caps = NULL;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL)
  {
    /* ERROR: could not get caps of pad */
    length = 0.0;
  }
  else
  {
    bytes = GST_BUFFER_SIZE (buf);
    width    = gst_caps_get_int (caps, "width");
    channels = gst_caps_get_int (caps, "channels");
    rate     = gst_caps_get_int (caps, "rate");

    length = (bytes * 8.0) / (double) (rate * channels * width);
  }
  return length;
}
