/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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



//#define DEBUG_ENABLED


#include <wine/winbase.h>
#include <wine/winerror.h>
#include <wine/driver.h>
#include <wine/msacm.h>
#define WIN32
#include <gstavidecoder.h>

typedef struct _GstWinLoaderAudioData GstWinLoaderAudioData;

struct _GstWinLoaderAudioData {
  guchar ext_info[64];
  WAVEFORMATEX wf;
  HACMSTREAM srcstream;
  GstPad *out;
};


static GstPad *gst_avi_decoder_get_audio_srcpad_MPEG(GstAviDecoder *avi_decoder, guint pad_nr, GstPadTemplate *temp);
static GstPad *gst_avi_decoder_get_audio_srcpad_winloader(GstAviDecoder *avi_decoder, guint pad_nr, gst_riff_strf_auds *strf, GstPadTemplate *temp);
static void gst_avi_decoder_winloader_audio_chain(GstPad *pad, GstBuffer *buf);

GstPad *gst_avi_decoder_get_audio_srcpad(GstAviDecoder *avi_decoder, guint pad_nr, gst_riff_strf_auds *strf, GstPadTemplate *temp) 
{
  GstPad *newpad;

  switch (strf->format) {
    case GST_RIFF_WAVE_FORMAT_PCM:
      newpad = gst_pad_new("audio_00", GST_PAD_SRC);
      gst_pad_try_set_caps (newpad, 
		            GST_CAPS_NEW (
			      "avidecoder_caps",
			      "audio/raw",
				 "format", 	GST_PROPS_STRING ("int"),
				 "law",  	GST_PROPS_INT (0),
				  "endianness", GST_PROPS_INT (G_BYTE_ORDER),
				  "signed",	GST_PROPS_BOOLEAN (TRUE),
				  "width",	GST_PROPS_INT ((gint)strf->size),
				  "depth",	GST_PROPS_INT ((gint)strf->size),
				  "rate",	GST_PROPS_INT ((gint)strf->rate),
				  "channels",	GST_PROPS_INT ((gint)strf->channels)
			    ));

      avi_decoder->audio_pad[pad_nr] = newpad;
      return newpad;
    case GST_RIFF_WAVE_FORMAT_MPEGL12:
    case GST_RIFF_WAVE_FORMAT_MPEGL3:
      return gst_avi_decoder_get_audio_srcpad_MPEG(avi_decoder, pad_nr, temp);
    default:
      newpad = gst_avi_decoder_get_audio_srcpad_winloader(avi_decoder, pad_nr, strf, temp); 
      if (newpad) return newpad;
      printf("audio format %04x not supported\n", strf->format);
      break;
  }
  return NULL;
}

static GstPad *gst_avi_decoder_get_audio_srcpad_MPEG(GstAviDecoder *avi_decoder, guint pad_nr, GstPadTemplate *temp) 
{
  GstElement *parse_audio, *decode;
  GstPad *srcpad, *sinkpad, *newpad;

  parse_audio = gst_elementfactory_make("mp3parse", "parse_audio");
  g_return_val_if_fail(parse_audio != NULL, NULL);
  decode = gst_elementfactory_make("mpg123", "decode_audio");
  g_return_val_if_fail(decode != NULL, NULL);

  gst_element_set_state(GST_ELEMENT(gst_object_get_parent(GST_OBJECT(avi_decoder))), GST_STATE_PAUSED);

  gst_bin_add(GST_BIN(gst_object_get_parent(GST_OBJECT(avi_decoder))), parse_audio);
  gst_bin_add(GST_BIN(gst_object_get_parent(GST_OBJECT(avi_decoder))), decode);

  newpad = gst_pad_new("video", GST_PAD_SRC);
  gst_pad_set_parent(newpad, GST_OBJECT(avi_decoder));

  sinkpad = gst_element_get_pad(parse_audio,"sink");
  gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_set_chain_function (gst_element_get_pad(parse_audio,"src"),
                              GST_RPAD_CHAINFUNC (gst_element_get_pad(decode,"sink")));
  srcpad = gst_element_get_pad(decode,"src");

  gst_pad_connect(newpad, sinkpad);
  gst_pad_set_name(srcpad, "audio_00");
  gst_pad_set_chain_function (newpad, GST_RPAD_CHAINFUNC (sinkpad));

  avi_decoder->audio_pad[pad_nr] = newpad;
  gst_element_set_state(GST_ELEMENT(gst_object_get_parent(GST_OBJECT(avi_decoder))), GST_STATE_PLAYING);

  return srcpad;
}

static GstPad *gst_avi_decoder_get_audio_srcpad_winloader(GstAviDecoder *avi_decoder, guint pad_nr, gst_riff_strf_auds *strf, GstPadTemplate *temp) 
{
  HRESULT h;
  GstWinLoaderAudioData *data;
  GstPad *sinkpad, *newpad;

  if (!gst_library_load("winloader")) {
    gst_info("audiocodecs: could not load support library: 'winloader'\n");
    return NULL;
  }
  gst_info("audiocodecs: winloader loaded\n");

  avi_decoder->extra_data = g_malloc0(sizeof(GstWinLoaderAudioData));

  data = (GstWinLoaderAudioData *)avi_decoder->extra_data;

  memcpy(data->ext_info, strf, sizeof(WAVEFORMATEX));
  memset(data->ext_info+18, 0, 32);

  if (strf->rate == 0) 
    return NULL;

  data->wf.nChannels=strf->channels;
  data->wf.nSamplesPerSec=strf->rate;
  data->wf.nAvgBytesPerSec=2*data->wf.nSamplesPerSec*data->wf.nChannels;
  data->wf.wFormatTag=strf->format;
  data->wf.nBlockAlign=strf->blockalign;
  data->wf.wBitsPerSample=strf->av_bps;
  data->wf.cbSize=0;

  gst_info("audiocodecs: trying to open library %p\n", data);
  h = acmStreamOpen(
           &data->srcstream,
           (HACMDRIVER)NULL,       
           (WAVEFORMATEX*)data->ext_info,  
           (WAVEFORMATEX*)&data->wf,  
           NULL,  
           0,     
           0,     
           0);

  if(h != S_OK)
  {
     if(h == ACMERR_NOTPOSSIBLE) {
       printf("audiocodecs:: Unappropriate audio format\n");
     }
     printf("audiocodecs:: acmStreamOpen error\n");
     return NULL;
  }

  newpad = gst_pad_new("audio", GST_PAD_SINK);
  gst_pad_set_parent(newpad, GST_OBJECT(avi_decoder));
  gst_pad_set_chain_function(newpad, gst_avi_decoder_winloader_audio_chain);

  sinkpad = gst_pad_new("audio_00", GST_PAD_SRC);
  gst_pad_set_parent(sinkpad, GST_OBJECT(avi_decoder));
  gst_pad_connect(newpad, sinkpad);
  gst_pad_set_chain_function (newpad, GST_RPAD_CHAINFUNC (sinkpad));

  //gst_pad_connect(newpad, sinkpad);
  avi_decoder->audio_pad[pad_nr] = newpad;

  data->out = sinkpad;

  GST_DEBUG (0,"gst_avi_decoder: pads created\n");
  return sinkpad;
}

static void gst_avi_decoder_winloader_audio_chain(GstPad *pad, GstBuffer *buf) 
{

  GST_DEBUG (0,"gst_avi_decoder: got buffer %08lx %p\n", *(gulong *)GST_BUFFER_DATA(buf), GST_BUFFER_DATA(buf));
  gst_buffer_unref(buf);  
}
