#ifndef __TARKIN_H
#define __TARKIN_H

#include <stdio.h>
#include "wavelet.h"
#include <ogg/ogg.h>


#define BUG(x...)                                                            \
   do {                                                                      \
      printf("BUG in %s (%s: line %i): ", __FUNCTION__, __FILE__, __LINE__); \
      printf(#x);                                                            \
      printf("\n");                                                          \
      exit (-1);                                                             \
   } while (0);


/* Theses determine what infos the packet comes with */
#define TARKIN_PACK_EXAMPLE                1

typedef struct
{
  uint8_t *data;
  uint32_t data_len;
  uint32_t storage;
} TarkinPacket;


typedef enum
{
  TARKIN_GRAYSCALE,
  TARKIN_RGB24,			/*  tight packed RGB        */
  TARKIN_RGB32,			/*  32bit, no alphachannel  */
  TARKIN_RGBA,			/*  dito w/ alphachannel    */
  TARKIN_YUV2,			/*  16 bits YUV             */
  TARKIN_YUV12,			/*  12 bits YUV             */
  TARKIN_FYUV,			/*  Tarkin's Fast YUV-like? */
} TarkinColorFormat;

#define TARKIN_INTERNAL_FORMAT TARKIN_FYUV

typedef enum
{
  TARKIN_OK = 0,
  TARKIN_IO_ERROR,
  TARKIN_SIGNATURE_NOT_FOUND,
  TARKIN_INVALID_LAYER,
  TARKIN_INVALID_COLOR_FORMAT,
  TARKIN_VERSION,
  TARKIN_BAD_HEADER,
  TARKIN_NOT_TARKIN,
  TARKIN_FAULT,
  TARKIN_UNUSED,
  TARKIN_NEED_MORE,
  TARKIN_NOT_IMPLEMENTED
} TarkinError;



typedef struct
{
  uint32_t width;
  uint32_t height;
  uint32_t a_moments;
  uint32_t s_moments;
  uint32_t frames_per_buf;
  uint32_t bitstream_len;	/*  for all color components, bytes */
  TarkinColorFormat format;
} TarkinVideoLayerDesc;


typedef struct
{
  TarkinVideoLayerDesc desc;
  uint32_t n_comp;		/*  number of color components */
  Wavelet3DBuf **waveletbuf;
  TarkinPacket *packet;
  uint32_t current_frame_in_buf;
  uint32_t frameno;

  void (*color_fwd_xform) (uint8_t * rgba, Wavelet3DBuf * yuva[],
      uint32_t count);
  void (*color_inv_xform) (Wavelet3DBuf * yuva[], uint8_t * rgba,
      uint32_t count);
} TarkinVideoLayer;

typedef struct
{
  uint32_t numerator;
  uint32_t denominator;
} TarkinTime;			/* Let's say the unit is 1 second */

typedef struct TarkinInfo
{
  int version;
  int n_layers;
  TarkinVideoLayer *layer;
  TarkinTime inter;		/* numerator == O if per-frame time info. */
  int frames_per_block;
  int comp_per_block;		/* AKA "packets per block" for now */
  uint32_t max_bitstream_len;

  /* The below bitrate declarations are *hints*.
     Combinations of the three values carry the following implications:

     all three set to the same value: 
     implies a fixed rate bitstream
     only nominal set: 
     implies a VBR stream that averages the nominal bitrate.  No hard 
     upper/lower limit
     upper and or lower set: 
     implies a VBR bitstream that obeys the bitrate limits. nominal 
     may also be set to give a nominal rate.
     none set:
     the coder does not care to speculate.
   */

  long bitrate_upper;
  long bitrate_nominal;
  long bitrate_lower;
  long bitrate_window;
} TarkinInfo;

/* This is used for encoding */
typedef struct
{
  unsigned char *header;
  unsigned char *header1;
  unsigned char *header2;
} tarkin_header_store;




 /* Some of the fields in TarkinStream are redundent with TarkinInfo ones
  * and will probably get deleted, namely n_layers and frames_per_buf */
typedef struct TarkinStream
{
  uint32_t n_layers;
  TarkinVideoLayer *layer;
  uint32_t current_frame;
  uint32_t current_frame_in_buf;
  ogg_int64_t packetno;
  uint32_t frames_per_buf;
  uint32_t max_bitstream_len;
  TarkinInfo *ti;
  tarkin_header_store headers;
  /* These callbacks are only used for encoding */
    TarkinError (*free_frame) (void *tarkinstream, void *ptr);
  /* These thing allows not to buffer but it needs global var in caller. */
    TarkinError (*packet_out) (void *tarkinstream, ogg_packet * ptr);
  void *user_ptr;
} TarkinStream;


typedef struct TarkinComment
{
  /* unlimited user comment fields.  libtarkin writes 'libtarkin'
     whatever vendor is set to in encode */
  char **user_comments;
  int *comment_lengths;
  int comments;
  char *vendor;

} TarkinComment;

/* Tarkin PRIMITIVES: general ***************************************/

/* The Tarkin header is in three packets, the initial small packet in
   the first page that identifies basic parameters, that is a TarkinInfo
   structure, a second packet with bitstream comments and a third packet 
   that holds the layers description structures. */


/* Theses are the very same than Vorbis versions, they could be shared. */
extern TarkinStream *tarkin_stream_new ();
extern void tarkin_stream_destroy (TarkinStream * s);
extern void tarkin_info_init (TarkinInfo * vi);
extern void tarkin_info_clear (TarkinInfo * vi);
extern void tarkin_comment_init (TarkinComment * vc);
extern void tarkin_comment_add (TarkinComment * vc, char *comment);
extern void tarkin_comment_add_tag (TarkinComment * vc,
    char *tag, char *contents);
extern char *tarkin_comment_query (TarkinComment * vc, char *tag, int count);
extern int tarkin_comment_query_count (TarkinComment * vc, char *tag);
extern void tarkin_comment_clear (TarkinComment * vc);

/* Tarkin PRIMITIVES: analysis layer ****************************/
/* Tarkin encoding is done this way : you init it passing a fresh
 * TarkinStream and a fresh TarkinInfo which has at least the rate_num
 * field renseigned. You also pass it two callback functions: free_frame()
 * is called when the lib doesn't need a frame anymore, and packet_out
 * is called when a packet is ready. The pointers given as arguments to 
 * these callback functions are of course only valid at the function call
 * time. The user_ptr is stored in s and can be used by packet_out(). */
extern int tarkin_analysis_init (TarkinStream * s,
    TarkinInfo * ti,
    TarkinError (*free_frame) (void *tarkinstream, void *ptr),
    TarkinError (*packet_out) (void *tarkinstream, ogg_packet * ptr),
    void *user_ptr);
/* Then you need to add at least a layer in your stream, passing a 
 * TarkinVideoLayerDesc renseigned at least on the width, height and
 * format parameters. */
extern int tarkin_analysis_add_layer (TarkinStream * s,
    TarkinVideoLayerDesc * tvld);
/* At that point you are ready to get headers out the lib by calling
 * tarkin_analysis_headerout() passing it a renseigned TarkinComment
 * structure. It does fill your 3 ogg_packet headers, which are valid
 * till next call */
extern int TarkinCommentheader_out (TarkinComment * vc, ogg_packet * op);
extern TarkinError tarkin_analysis_headerout (TarkinStream * s,
    TarkinComment * vc,
    ogg_packet * op, ogg_packet * op_comm, ogg_packet * op_code);
/* You are now ready to pass in frames to the codec, however don't free
 * them before the codec told you so. It'll tell you when packets are
 * ready to be taken out. When you have no more frame, simply pass NULL.
 * If you encode multiple layers you have to do it synchronously, putting 
 * one frame from each layer at a time. */
extern uint32_t tarkin_analysis_framein (TarkinStream * s, uint8_t * frame,	/* NULL for EOS */
    uint32_t layer, TarkinTime * date);

/* Tarkin PRIMITIVES: synthesis layer *******************************/
/* For decoding, you needs first to give the three first packet of the 
 * stream to tarkin_synthesis_headerin() which will fill for you blank
 * TarkinInfo and TarkinComment. */
extern TarkinError tarkin_synthesis_headerin (TarkinInfo * vi,
    TarkinComment * vc, ogg_packet * op);
/* Then you can init your stream with your TarkinInfo struct. */
extern TarkinError tarkin_synthesis_init (TarkinStream * s, TarkinInfo * ti);

/* All subsequent packets are to this be passed to tarkin_synthesis_packetin*/
extern TarkinError tarkin_synthesis_packetin (TarkinStream * s,
    ogg_packet * op);
/* and then tarkin_synthesis_frameout gives you ptr on next frame, or NULL. It
 * also fills for you date. */
extern TarkinError tarkin_synthesis_frameout (TarkinStream * s,
    uint8_t ** frame, uint32_t layer_id, TarkinTime * date);
/* When you're done with a frame, tell it to the codec with this. */
extern int tarkin_synthesis_freeframe (TarkinStream * s, uint8_t * frame);


#endif
