/*
 * cutter.h header file
 * thomas@apestaart.org
 */

typedef struct
{
  GstElement *pipe;
  GstElement *disksink;
  GstElement *audiosink;

  char *location;
  int channel_id;
}
output_channel_t;
