/*
 * mixer.h header file
 * thomas@apestaart.org
 */

typedef struct 
{
  GstElement *pipe, *disksrc, *volenv;
  
  char *location;
  int channel_id;
} input_channel_t;
