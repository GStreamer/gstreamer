/*
 * mixer.h header file
 * thomas@apestaart.org
 */

typedef struct 
{
  GstElement *pipe;
  
  GstElement *disksrc;
  GstElement *decoder;
  GstElement *volenv;
} input_pipe_t;
