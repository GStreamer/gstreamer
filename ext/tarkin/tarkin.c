/*
 *   The real io-stuff is in tarkin-io.c
 *   (this one has to be rewritten to write ogg streams ...)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mem.h"
#include "tarkin.h"
#include "yuv.h"


#define N_FRAMES 1



TarkinStream *
tarkin_stream_new ()
{
  TarkinStream *s = (TarkinStream *) CALLOC (1, sizeof (TarkinStream));

  if (!s)
    return NULL;
  memset (s, 0, sizeof (*s));

  s->frames_per_buf = N_FRAMES;

  return s;
}


void
tarkin_stream_destroy (TarkinStream * s)
{
  uint32_t i, j;

  if (!s)
    return;

  for (i = 0; i < s->n_layers; i++) {
    if (s->layer[i].waveletbuf) {
      for (j = 0; j < s->layer[i].n_comp; j++) {
	wavelet_3d_buf_destroy (s->layer[i].waveletbuf[j]);
	FREE (s->layer[i].packet[j].data);
      }
      FREE (s->layer[i].waveletbuf);
      FREE (s->layer[i].packet);
    }
  }

  if (s->layer)
    FREE (s->layer);

  if (s->headers.header)
    FREE (s->headers.header);

  if (s->headers.header1)
    FREE (s->headers.header1);

  if (s->headers.header2)
    FREE (s->headers.header2);


  FREE (s);
}


int
tarkin_analysis_init (TarkinStream * s, TarkinInfo * ti,
    TarkinError (*free_frame) (void *s, void *ptr),
    TarkinError (*packet_out) (void *s, ogg_packet * ptr), void *user_ptr)
{
  if ((!ti->inter.numerator) || (!ti->inter.denominator))
    return (-TARKIN_FAULT);
  if ((!free_frame) || (!packet_out))
    return (-TARKIN_FAULT);
  s->ti = ti;
  s->free_frame = free_frame;
  s->packet_out = packet_out;
  s->user_ptr = user_ptr;
  return (0);
}


extern int
tarkin_analysis_add_layer (TarkinStream * s, TarkinVideoLayerDesc * tvld)
{
  int i;
  TarkinVideoLayer *layer;

  if (s->n_layers) {
    s->layer = REALLOC (s->layer, (s->n_layers + 1) * sizeof (*s->layer));
  } else {
    s->layer = MALLOC (sizeof (*s->layer));
  }
  layer = s->layer + s->n_layers;
  memset (layer, 0, sizeof (*s->layer));
  memcpy (&layer->desc, tvld, sizeof (TarkinVideoLayerDesc));

  s->n_layers++;
  s->ti->n_layers = s->n_layers;
  s->ti->layer = s->layer;

  switch (layer->desc.format) {
    case TARKIN_GRAYSCALE:
      layer->n_comp = 1;
      layer->color_fwd_xform = grayscale_to_y;
      layer->color_inv_xform = y_to_grayscale;
      break;
    case TARKIN_RGB24:
      layer->n_comp = 3;
      layer->color_fwd_xform = rgb24_to_yuv;
      layer->color_inv_xform = yuv_to_rgb24;
      break;
    case TARKIN_RGB32:
      layer->n_comp = 3;
      layer->color_fwd_xform = rgb32_to_yuv;
      layer->color_inv_xform = yuv_to_rgb32;
      break;
    case TARKIN_RGBA:
      layer->n_comp = 4;
      layer->color_fwd_xform = rgba_to_yuv;
      layer->color_inv_xform = yuv_to_rgba;
      break;
    default:
      return -TARKIN_INVALID_COLOR_FORMAT;
  };

#ifdef DBG_OGG
  printf ("dbg_ogg:add_layer %d with %d components\n",
      s->n_layers, layer->n_comp);
#endif

  layer->waveletbuf = (Wavelet3DBuf **) CALLOC (layer->n_comp,
      sizeof (Wavelet3DBuf *));

  layer->packet = MALLOC (layer->n_comp * sizeof (*layer->packet));
  memset (layer->packet, 0, layer->n_comp * sizeof (*layer->packet));

  for (i = 0; i < layer->n_comp; i++) {
    layer->waveletbuf[i] = wavelet_3d_buf_new (layer->desc.width,
	layer->desc.height, layer->desc.frames_per_buf);
    layer->packet[i].data = MALLOC (layer->desc.bitstream_len);
    layer->packet[i].storage = layer->desc.bitstream_len;
  }
  /*
     max_bitstream_len += layer->desc.bitstream_len
     + 2 * 10 * sizeof(uint32_t) * layer->n_comp;    
   */
  return (TARKIN_OK);
}

TarkinError
_analysis_packetout (TarkinStream * s, uint32_t layer_id, uint32_t comp)
{
  ogg_packet op;
  oggpack_buffer opb;
  uint8_t *data;
  uint32_t data_len;
  int i;

  data = s->layer[layer_id].packet[comp].data;
  data_len = s->layer[layer_id].packet[comp].data_len;

  oggpack_writeinit (&opb);
  oggpack_write (&opb, 0, 8);	/* No feature flags for now */
  oggpack_write (&opb, layer_id, 12);
  oggpack_write (&opb, comp, 12);
  for (i = 0; i < data_len; i++)
    oggpack_write (&opb, *(data + i), 8);

  op.b_o_s = 0;
  op.e_o_s = data_len ? 0 : 1;
  op.granulepos = 0;
  op.bytes = oggpack_bytes (&opb) + 4;
  op.packet = opb.buffer;
#ifdef DBG_OGG
  printf ("dbg_ogg: writing packet layer %d, comp %d, data_len %d %s\n",
      layer_id, comp, data_len, op.e_o_s ? "eos" : "");
#endif
  s->layer[layer_id].packet[comp].data_len = 0;	/* so direct call => eos */
  return (s->packet_out (s, &op));
}

void
_stream_flush (TarkinStream * s)
{
  uint32_t i, j;

  s->current_frame_in_buf = 0;

  for (i = 0; i < s->n_layers; i++) {
    TarkinVideoLayer *layer = &s->layer[i];

    for (j = 0; j < layer->n_comp; j++) {
      uint32_t comp_bitstream_len;
      TarkinPacket *packet = layer->packet + j;

	/**
         *  implicit 6:1:1 subsampling
         */
      if (j == 0)
	comp_bitstream_len =
	    6 * layer->desc.bitstream_len / (layer->n_comp + 5);
      else
	comp_bitstream_len = layer->desc.bitstream_len / (layer->n_comp + 5);

      if (packet->storage < comp_bitstream_len) {
	packet->storage = comp_bitstream_len;
	packet->data = REALLOC (packet->data, comp_bitstream_len);
      }

      wavelet_3d_buf_dump ("color-%d-%03d.pgm",
	  s->current_frame, j, layer->waveletbuf[j], j == 0 ? 0 : 128);

      wavelet_3d_buf_fwd_xform (layer->waveletbuf[j],
	  layer->desc.a_moments, layer->desc.s_moments);

      wavelet_3d_buf_dump ("coeff-%d-%03d.pgm",
	  s->current_frame, j, layer->waveletbuf[j], 128);

      packet->data_len = wavelet_3d_buf_encode_coeff (layer->waveletbuf[j],
	  packet->data, comp_bitstream_len);

      _analysis_packetout (s, i, j);
    }
  }
}


uint32_t
tarkin_analysis_framein (TarkinStream * s, uint8_t * frame,
    uint32_t layer_id, TarkinTime * date)
{
  TarkinVideoLayer *layer;

  if (!frame)
    return (_analysis_packetout (s, 0, 0));	/* eos */
  if ((layer_id >= s->n_layers) || (date->denominator == 0))
    return (TARKIN_FAULT);

  layer = s->layer + layer_id;
  layer->color_fwd_xform (frame, layer->waveletbuf, s->current_frame_in_buf);
  /* We don't use this feature for now, neither date... */
  s->free_frame (s, frame);

  s->current_frame_in_buf++;

  if (s->current_frame_in_buf == s->frames_per_buf)
    _stream_flush (s);

#ifdef DBG_OGG
  printf ("dbg_ogg: framein at pos %d/%d, n° %d,%d on layer %d\n",
      date->numerator, date->denominator,
      layer->frameno, s->current_frame, layer_id);
#endif

  layer->frameno++;
  return (++s->current_frame);
}




/**
 *   tarkin_stream_read_header() is now info.c:_tarkin_unpack_layer_desc()
 */



TarkinError
tarkin_stream_get_layer_desc (TarkinStream * s,
    uint32_t layer_id, TarkinVideoLayerDesc * desc)
{
  if (layer_id > s->n_layers - 1)
    return -TARKIN_INVALID_LAYER;

  memcpy (desc, &(s->layer[layer_id].desc), sizeof (TarkinVideoLayerDesc));

  return TARKIN_OK;
}

TarkinError
tarkin_synthesis_init (TarkinStream * s, TarkinInfo * ti)
{
  s->ti = ti;
  s->layer = ti->layer;		/* It was malloc()ed by headerin() */
  s->n_layers = ti->n_layers;
  return (TARKIN_OK);
}

TarkinError
tarkin_synthesis_packetin (TarkinStream * s, ogg_packet * op)
{
  uint32_t i, layer_id, comp, data_len;
  uint32_t flags, junk;
  int nread;
  oggpack_buffer opb;
  TarkinPacket *packet;

#ifdef DBG_OGG
  printf ("dbg_ogg: Reading packet n° %lld, granulepos %lld, len %ld, %s%s\n",
      op->packetno, op->granulepos, op->bytes,
      op->b_o_s ? "b_o_s" : "", op->e_o_s ? "e_o_s" : "");
#endif
  oggpack_readinit (&opb, op->packet, op->bytes);
  flags = oggpack_read (&opb, 8);
  layer_id = oggpack_read (&opb, 12);	/* Theses are  required for */
  comp = oggpack_read (&opb, 12);	/* data hole handling (or maybe
					 * packetno would be enough ?) */
  nread = 4;

  if (flags) {			/* This is void "infinite future features" feature ;) */
    if (flags & 1 << 7) {
      junk = flags;
      while (junk & 1 << 7)
	junk = oggpack_read (&opb, 8);	/* allow for many future flags
					   that must be correctly ordonned. */
    }
    /* This shows how to get a feature's data:
       if (flags & TARKIN_FLAGS_EXAMPLE){
       tp->example = oggpack_read(&opb,32);
       junk = tp->example & 3<<30;
       tp->example &= 0x4fffffff;
       }
     */
    for (junk = 1 << 31; junk & 1 << 31;)	/* and many future data */
      while ((junk = oggpack_read (&opb, 32)) & 1 << 30);
    /* That is, feature data comes in 30 bit chunks. We also have
     * 31 potentially usefull bits in last chunk. */
  }

  nread = (opb.ptr - opb.buffer);
  data_len = op->bytes - nread;

#ifdef DBG_OGG
  printf ("   layer_id %d, comp %d, meta-data %dB, w3d data %dB.\n",
      layer_id, comp, nread, data_len);
#endif

  /* We now have for shure our data. */
  packet = &s->layer[layer_id].packet[comp];
  if (packet->data_len)
    return (-TARKIN_UNUSED);	/* Previous data wasn't used */

  if (packet->storage < data_len) {
    packet->storage = data_len + 255;
    packet->data = REALLOC (packet->data, packet->storage);
  }

  for (i = 0; i < data_len; i++)
    packet->data[i] = oggpack_read (&opb, 8);

  packet->data_len = data_len;

  return (TARKIN_OK);
}

TarkinError
tarkin_synthesis_frameout (TarkinStream * s,
    uint8_t ** frame, uint32_t layer_id, TarkinTime * date)
{
  int j;
  TarkinVideoLayer *layer = &s->layer[layer_id];

  if (s->current_frame_in_buf == 0) {
    *frame = MALLOC (layer->desc.width * layer->desc.height * layer->n_comp);
    for (j = 0; j < layer->n_comp; j++) {
      TarkinPacket *packet = layer->packet + j;

      if (packet->data_len == 0)
	goto err_out;

      wavelet_3d_buf_decode_coeff (layer->waveletbuf[j], packet->data,
	  packet->data_len);

      wavelet_3d_buf_dump ("rcoeff-%d-%03d.pgm",
	  s->current_frame, j, layer->waveletbuf[j], 128);

      wavelet_3d_buf_inv_xform (layer->waveletbuf[j],
	  layer->desc.a_moments, layer->desc.s_moments);

      wavelet_3d_buf_dump ("rcolor-%d-%03d.pgm",
	  s->current_frame, j, layer->waveletbuf[j], j == 0 ? 0 : 128);
    }

    /* We did successfylly read a block from this layer, acknowledge it. */
    for (j = 0; j < layer->n_comp; j++)
      layer->packet[j].data_len = 0;
  }

  layer->color_inv_xform (layer->waveletbuf, *frame, s->current_frame_in_buf);
  s->current_frame_in_buf++;
  s->current_frame++;

  if (s->current_frame_in_buf == s->frames_per_buf)
    s->current_frame_in_buf = 0;

  date->numerator = layer->frameno * s->ti->inter.numerator;
  date->denominator = s->ti->inter.denominator;
#ifdef DBG_OGG
  printf ("dbg_ogg: outputting frame pos %d/%d from layer %d.\n",
      date->numerator, date->denominator, layer_id);
#endif
  layer->frameno++;
  return (TARKIN_OK);
err_out:
  FREE (*frame);
  return (TARKIN_NEED_MORE);
}

int
tarkin_synthesis_freeframe (TarkinStream * s, uint8_t * frame)
{
  FREE (frame);

  return (TARKIN_OK);
}
