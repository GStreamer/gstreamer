/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2001             *
 * by the XIPHOPHORUS Company http://www.xiph.org/                  *

 ********************************************************************

 function: maintain the info structure, info <-> header packets
 last mod: $Id$

 ********************************************************************/

/* general handling of the header and the TarkinInfo structure (and
   substructures) */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <ogg/ogg.h>
#include "tarkin.h"
#include "yuv.h"
#include "mem.h"

/* helpers */
static void
_v_writestring (oggpack_buffer * o, char *s, int bytes)
{
  while (bytes--) {
    oggpack_write (o, *s++, 8);
  }
}

static void
_v_readstring (oggpack_buffer * o, char *buf, int bytes)
{
  while (bytes--) {
    *buf++ = oggpack_read (o, 8);
  }
}

void
tarkin_comment_init (TarkinComment * vc)
{
  memset (vc, 0, sizeof (*vc));
}

void
tarkin_comment_add (TarkinComment * vc, char *comment)
{
  vc->user_comments = REALLOC (vc->user_comments,
      (vc->comments + 2) * sizeof (*vc->user_comments));
  vc->comment_lengths = REALLOC (vc->comment_lengths,
      (vc->comments + 2) * sizeof (*vc->comment_lengths));
  vc->comment_lengths[vc->comments] = strlen (comment);
  vc->user_comments[vc->comments] =
      MALLOC (vc->comment_lengths[vc->comments] + 1);
  strcpy (vc->user_comments[vc->comments], comment);
  vc->comments++;
  vc->user_comments[vc->comments] = NULL;
}

void
tarkin_comment_add_tag (TarkinComment * vc, char *tag, char *contents)
{
  char *comment = alloca (strlen (tag) + strlen (contents) + 2);	/* +2 for = and \0 */

  strcpy (comment, tag);
  strcat (comment, "=");
  strcat (comment, contents);
  tarkin_comment_add (vc, comment);
}

/* This is more or less the same as strncasecmp - but that doesn't exist
 * everywhere, and this is a fairly trivial function, so we include it */
static int
tagcompare (const char *s1, const char *s2, int n)
{
  int c = 0;

  while (c < n) {
    if (toupper (s1[c]) != toupper (s2[c]))
      return !0;
    c++;
  }
  return 0;
}

char *
tarkin_comment_query (TarkinComment * vc, char *tag, int count)
{
  long i;
  int found = 0;
  int taglen = strlen (tag) + 1;	/* +1 for the = we append */
  char *fulltag = alloca (taglen + 1);

  strcpy (fulltag, tag);
  strcat (fulltag, "=");

  for (i = 0; i < vc->comments; i++) {
    if (!tagcompare (vc->user_comments[i], fulltag, taglen)) {
      if (count == found)
	/* We return a pointer to the data, not a copy */
	return vc->user_comments[i] + taglen;
      else
	found++;
    }
  }
  return NULL;			/* didn't find anything */
}

int
tarkin_comment_query_count (TarkinComment * vc, char *tag)
{
  int i, count = 0;
  int taglen = strlen (tag) + 1;	/* +1 for the = we append */
  char *fulltag = alloca (taglen + 1);

  strcpy (fulltag, tag);
  strcat (fulltag, "=");

  for (i = 0; i < vc->comments; i++) {
    if (!tagcompare (vc->user_comments[i], fulltag, taglen))
      count++;
  }

  return count;
}

void
tarkin_comment_clear (TarkinComment * vc)
{
  if (vc) {
    long i;

    for (i = 0; i < vc->comments; i++)
      if (vc->user_comments[i])
	FREE (vc->user_comments[i]);
    if (vc->user_comments)
      FREE (vc->user_comments);
    if (vc->comment_lengths)
      FREE (vc->comment_lengths);
    if (vc->vendor)
      FREE (vc->vendor);
  }
  memset (vc, 0, sizeof (*vc));
}

/* used by synthesis, which has a full, alloced vi */
void
tarkin_info_init (TarkinInfo * vi)
{
  memset (vi, 0, sizeof (*vi));
}

void
tarkin_info_clear (TarkinInfo * vi)
{
  memset (vi, 0, sizeof (*vi));
}

/* Header packing/unpacking ********************************************/

static int
_tarkin_unpack_info (TarkinInfo * vi, oggpack_buffer * opb)
{
#ifdef DBG_OGG
  printf ("dbg_ogg: Decoding Info: ");
#endif
  vi->version = oggpack_read (opb, 32);
  if (vi->version != 0)
    return (-TARKIN_VERSION);

  vi->n_layers = oggpack_read (opb, 8);
  vi->inter.numerator = oggpack_read (opb, 32);
  vi->inter.denominator = oggpack_read (opb, 32);

  vi->bitrate_upper = oggpack_read (opb, 32);
  vi->bitrate_nominal = oggpack_read (opb, 32);
  vi->bitrate_lower = oggpack_read (opb, 32);

#ifdef DBG_OGG
  printf (" n_layers %d, interleave: %d/%d, ",
      vi->n_layers, vi->inter.numerator, vi->inter.denominator);
#endif

  if (vi->inter.numerator < 1)
    goto err_out;
  if (vi->inter.denominator < 1)
    goto err_out;
  if (vi->n_layers < 1)
    goto err_out;

  if (oggpack_read (opb, 1) != 1)
    goto err_out;		/* EOP check */

#ifdef DBG_OGG
  printf ("Success\n");
#endif
  return (0);
err_out:
#ifdef DBG_OGG
  printf ("Failed\n");
#endif
  tarkin_info_clear (vi);
  return (-TARKIN_BAD_HEADER);
}

static int
_tarkin_unpack_comment (TarkinComment * vc, oggpack_buffer * opb)
{
  int i;
  int vendorlen = oggpack_read (opb, 32);

#ifdef DBG_OGG
  printf ("dbg_ogg: Decoding comment: ");
#endif
  if (vendorlen < 0)
    goto err_out;
  vc->vendor = _ogg_calloc (vendorlen + 1, 1);
  _v_readstring (opb, vc->vendor, vendorlen);
  vc->comments = oggpack_read (opb, 32);
  if (vc->comments < 0)
    goto err_out;
  vc->user_comments =
      _ogg_calloc (vc->comments + 1, sizeof (*vc->user_comments));
  vc->comment_lengths =
      _ogg_calloc (vc->comments + 1, sizeof (*vc->comment_lengths));

  for (i = 0; i < vc->comments; i++) {
    int len = oggpack_read (opb, 32);

    if (len < 0)
      goto err_out;
    vc->comment_lengths[i] = len;
    vc->user_comments[i] = _ogg_calloc (len + 1, 1);
    _v_readstring (opb, vc->user_comments[i], len);
  }
  if (oggpack_read (opb, 1) != 1)
    goto err_out;		/* EOP check */

#ifdef DBG_OGG
  printf ("Success, read %d comments\n", vc->comments);
#endif
  return (0);
err_out:
#ifdef DBG_OGG
  printf ("Failed\n");
#endif
  tarkin_comment_clear (vc);
  return (-TARKIN_BAD_HEADER);
}

/*  the real encoding details are here, currently TarkinVideoLayerDesc. */
static int
_tarkin_unpack_layer_desc (TarkinInfo * vi, oggpack_buffer * opb)
{
  int i, j;

  vi->layer = CALLOC (vi->n_layers, (sizeof (*vi->layer)));
  memset (vi->layer, 0, vi->n_layers * sizeof (*vi->layer));

#ifdef DBG_OGG
  printf ("ogg: Decoding layers description: ");
#endif
  for (i = 0; i < vi->n_layers; i++) {
    TarkinVideoLayer *layer = vi->layer + i;

    layer->desc.width = oggpack_read (opb, 32);
    layer->desc.height = oggpack_read (opb, 32);
    layer->desc.a_moments = oggpack_read (opb, 32);
    layer->desc.s_moments = oggpack_read (opb, 32);
    layer->desc.frames_per_buf = oggpack_read (opb, 32);
    layer->desc.bitstream_len = oggpack_read (opb, 32);
    layer->desc.format = oggpack_read (opb, 32);

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

    layer->waveletbuf = (Wavelet3DBuf **) CALLOC (layer->n_comp,
	sizeof (Wavelet3DBuf *));

    layer->packet = MALLOC (layer->n_comp * sizeof (*layer->packet));
    memset (layer->packet, 0, layer->n_comp * sizeof (*layer->packet));

    for (j = 0; j < layer->n_comp; j++) {
      layer->waveletbuf[j] = wavelet_3d_buf_new (layer->desc.width,
	  layer->desc.height, layer->desc.frames_per_buf);
      layer->packet[j].data = MALLOC (layer->desc.bitstream_len);
      layer->packet[j].storage = layer->desc.bitstream_len;
    }

    vi->max_bitstream_len += layer->desc.bitstream_len + 2 * 10 * sizeof (uint32_t) * layer->n_comp;	/* truncation tables  */

#ifdef DBG_OGG
    printf
	("\n     layer%d: size %dx%dx%d, format %d, a_m %d, s_m %d, %d fpb\n",
	i, layer->desc.width, layer->desc.height, layer->n_comp,
	layer->desc.format, layer->desc.a_moments, layer->desc.s_moments,
	layer->desc.frames_per_buf);
#endif
  }				/* for each layer */

  if (oggpack_read (opb, 1) != 1)
    goto err_out;		/* EOP check */

#ifdef DBG_OGG
  printf ("Success\n");
#endif

  return (0);
err_out:
#ifdef DBG_OGG
  printf ("Failed\n");
#endif
  tarkin_info_clear (vi);
  return (-TARKIN_BAD_HEADER);
}

/* The Tarkin header is in three packets; the initial small packet in
   the first page that identifies basic parameters, a second packet
   with bitstream comments and a third packet that holds the
   layer description structures. */

TarkinError
tarkin_synthesis_headerin (TarkinInfo * vi, TarkinComment * vc, ogg_packet * op)
{
  oggpack_buffer opb;

  if (op) {
    oggpack_readinit (&opb, op->packet, op->bytes);

    /* Which of the three types of header is this? */
    /* Also verify header-ness, tarkin */
    {
      char buffer[6];
      int packtype = oggpack_read (&opb, 8);

      memset (buffer, 0, 6);
      _v_readstring (&opb, buffer, 6);
      if (memcmp (buffer, "tarkin", 6)) {
	/* not a tarkin header */
	return (-TARKIN_NOT_TARKIN);
      }
      switch (packtype) {
	case 0x01:		/* least significant *bit* is read first */
	  if (!op->b_o_s) {
	    /* Not the initial packet */
	    return (-TARKIN_BAD_HEADER);
	  }
	  if (vi->inter.numerator != 0) {
	    /* previously initialized info header */
	    return (-TARKIN_BAD_HEADER);
	  }

	  return (_tarkin_unpack_info (vi, &opb));

	case 0x03:		/* least significant *bit* is read first */
	  if (vi->inter.denominator == 0) {
	    /* um... we didn't get the initial header */
	    return (-TARKIN_BAD_HEADER);
	  }

	  return (_tarkin_unpack_comment (vc, &opb));

	case 0x05:		/* least significant *bit* is read first */
	  if (vi->inter.numerator == 0 || vc->vendor == NULL) {
	    /* um... we didn;t get the initial header or comments yet */
	    return (-TARKIN_BAD_HEADER);
	  }

	  return (_tarkin_unpack_layer_desc (vi, &opb));

	default:
	  /* Not a valid tarkin header type */
	  return (-TARKIN_BAD_HEADER);
	  break;
      }
    }
  }
  return (-TARKIN_BAD_HEADER);
}

/* pack side **********************************************************/

static int
_tarkin_pack_info (oggpack_buffer * opb, TarkinInfo * vi)
{

  /* preamble */
  oggpack_write (opb, 0x01, 8);
  _v_writestring (opb, "tarkin", 6);

  /* basic information about the stream */
  oggpack_write (opb, 0x00, 32);
  oggpack_write (opb, vi->n_layers, 8);
  oggpack_write (opb, vi->inter.numerator, 32);
  oggpack_write (opb, vi->inter.denominator, 32);

  oggpack_write (opb, vi->bitrate_upper, 32);
  oggpack_write (opb, vi->bitrate_nominal, 32);
  oggpack_write (opb, vi->bitrate_lower, 32);

  oggpack_write (opb, 1, 1);

#ifdef DBG_OGG
  printf ("dbg_ogg: Putting out info, inter %d/%d, n_layers %d\n",
      vi->inter.numerator, vi->inter.denominator, vi->n_layers);
#endif
  return (0);
}

static int
_tarkin_pack_comment (oggpack_buffer * opb, TarkinComment * vc)
{
  char temp[] = "libTarkin debugging edition 20011104";
  int bytes = strlen (temp);

  /* preamble */
  oggpack_write (opb, 0x03, 8);
  _v_writestring (opb, "tarkin", 6);

  /* vendor */
  oggpack_write (opb, bytes, 32);
  _v_writestring (opb, temp, bytes);

  /* comments */

  oggpack_write (opb, vc->comments, 32);
  if (vc->comments) {
    int i;

    for (i = 0; i < vc->comments; i++) {
      if (vc->user_comments[i]) {
	oggpack_write (opb, vc->comment_lengths[i], 32);
	_v_writestring (opb, vc->user_comments[i], vc->comment_lengths[i]);
      } else {
	oggpack_write (opb, 0, 32);
      }
    }
  }
  oggpack_write (opb, 1, 1);

#ifdef DBG_OGG
  printf ("dbg_ogg: Putting out %d comments\n", vc->comments);
#endif

  return (0);
}

static int
_tarkin_pack_layer_desc (oggpack_buffer * opb, TarkinInfo * vi)
{
  int i;
  TarkinVideoLayer *layer;

#ifdef DBG_OGG
  printf ("dbg_ogg: Putting out layers description:\n");
#endif

  oggpack_write (opb, 0x05, 8);
  _v_writestring (opb, "tarkin", 6);

  for (i = 0; i < vi->n_layers; i++) {
    layer = vi->layer + i;
    oggpack_write (opb, layer->desc.width, 32);
    oggpack_write (opb, layer->desc.height, 32);
    oggpack_write (opb, layer->desc.a_moments, 32);
    oggpack_write (opb, layer->desc.s_moments, 32);
    oggpack_write (opb, layer->desc.frames_per_buf, 32);
    oggpack_write (opb, layer->desc.bitstream_len, 32);
    oggpack_write (opb, layer->desc.format, 32);

#ifdef DBG_OGG
    printf ("       res. %dx%d, format %d, a_m %d, s_m %d, fpb %d\n",
	layer->desc.width, layer->desc.height, layer->desc.format,
	layer->desc.a_moments, layer->desc.s_moments,
	layer->desc.frames_per_buf);
#endif

  }
  oggpack_write (opb, 1, 1);

#ifdef DBG_OGG
  printf ("      wrote %ld bytes.\n", oggpack_bytes (opb));
#endif

  return (0);
}

int
tarkin_comment_header_out (TarkinComment * vc, ogg_packet * op)
{

  oggpack_buffer opb;

  oggpack_writeinit (&opb);
  if (_tarkin_pack_comment (&opb, vc))
    return -TARKIN_NOT_IMPLEMENTED;

  op->packet = MALLOC (oggpack_bytes (&opb));
  memcpy (op->packet, opb.buffer, oggpack_bytes (&opb));

  op->bytes = oggpack_bytes (&opb);
  op->b_o_s = 0;
  op->e_o_s = 0;
  op->granulepos = 0;

  return 0;
}

TarkinError
tarkin_analysis_headerout (TarkinStream * v,
    TarkinComment * vc,
    ogg_packet * op, ogg_packet * op_comm, ogg_packet * op_code)
{
  int ret = -TARKIN_NOT_IMPLEMENTED;
  TarkinInfo *vi;
  oggpack_buffer opb;
  tarkin_header_store *b = &v->headers;

  vi = v->ti;

  /* first header packet ********************************************* */

  oggpack_writeinit (&opb);
  if (_tarkin_pack_info (&opb, vi))
    goto err_out;

  /* build the packet */
  if (b->header)
    FREE (b->header);
  b->header = MALLOC (oggpack_bytes (&opb));
  memcpy (b->header, opb.buffer, oggpack_bytes (&opb));
  op->packet = b->header;
  op->bytes = oggpack_bytes (&opb);
  op->b_o_s = 1;
  op->e_o_s = 0;
  op->granulepos = 0;

  /* second header packet (comments) ********************************* */

  oggpack_reset (&opb);
  if (_tarkin_pack_comment (&opb, vc))
    goto err_out;

  if (b->header1)
    FREE (b->header1);
  b->header1 = MALLOC (oggpack_bytes (&opb));
  memcpy (b->header1, opb.buffer, oggpack_bytes (&opb));
  op_comm->packet = b->header1;
  op_comm->bytes = oggpack_bytes (&opb);
  op_comm->b_o_s = 0;
  op_comm->e_o_s = 0;
  op_comm->granulepos = 0;

  /* third header packet (modes/codebooks) *************************** */

  oggpack_reset (&opb);
  if (_tarkin_pack_layer_desc (&opb, vi))
    goto err_out;

  if (b->header2)
    FREE (b->header2);
  b->header2 = MALLOC (oggpack_bytes (&opb));
  memcpy (b->header2, opb.buffer, oggpack_bytes (&opb));
  op_code->packet = b->header2;
  op_code->bytes = oggpack_bytes (&opb);
  op_code->b_o_s = 0;
  op_code->e_o_s = 0;
  op_code->granulepos = 0;

  oggpack_writeclear (&opb);
  return (0);
err_out:
  oggpack_writeclear (&opb);
  memset (op, 0, sizeof (*op));
  memset (op_comm, 0, sizeof (*op_comm));
  memset (op_code, 0, sizeof (*op_code));

  if (b->header)
    FREE (b->header);
  if (b->header1)
    FREE (b->header1);
  if (b->header2)
    FREE (b->header2);
  b->header = NULL;
  b->header1 = NULL;
  b->header2 = NULL;
  return (ret);
}
