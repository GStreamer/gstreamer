#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include "artsflow.h"
#include "stdsynthmodule.h"
#include "gst_artsio.h"
#include "convert.h"
#include "connect.h"
#include "flowsystem.h"

#include <gst/gst.h>

using namespace Arts;

namespace Gst
{

  class ArtsStereoSink_impl:virtual public ArtsStereoSink_skel,
      virtual public StdSynthModule
  {

    GstPad *sinkpad;
    GstPad *srcpad;
    unsigned long remainingsamples;
    GstData *inbuf;
    unsigned char *dataptr;

  public:

      ArtsStereoSink_impl ()
    {
      remainingsamples = 0;
      inbuf = NULL;
      dataptr = NULL;
    }

    void calculateBlock (unsigned long samples)
    {
      unsigned long fulfilled = 0;
//gint16 *s;
//fprintf(stderr,"StereoSink: getting %d samples\n",samples);

      while (fulfilled < samples)
      {
        if (remainingsamples == 0) {
//fprintf(stderr,"need to get a buffer\n");
          if (inbuf) {
            gst_data_unref (inbuf);
            inbuf = NULL;
          }
          // start by pulling a buffer from GStreamer
          inbuf = gst_pad_pull (sinkpad);

          while (GST_IS_EVENT (inbuf)) {
            switch (GST_EVENT_TYPE (inbuf)) {
              case GST_EVENT_EOS:
                gst_element_set_eos (GST_PAD_PARENT (sinkpad));
              default:
                break;
            }
            gst_pad_event_default (srcpad, GST_EVENT (inbuf));
            inbuf = gst_pad_pull (sinkpad);
          }

          dataptr = GST_BUFFER_DATA (GST_BUFFER (inbuf));
          remainingsamples = GST_BUFFER_SIZE (GST_BUFFER (inbuf)) / 4;
//fprintf(stderr,"got a buffer with %d samples\n",remainingsamples);
        }

        unsigned long count = MIN (remainingsamples, samples - fulfilled);

//fprintf(stderr,"have %d samples left, can fill %d\n",remainingsamples,count);
        convert_stereo_i16le_2float (count, dataptr, outleft, outright);
//s = (gint16 *)dataptr;
//fprintf(stderr,"samples in are %d and %d, out are %f and %f\n",s[0],s[1],outleft[0],outright[0]);
        remainingsamples -= count;
        dataptr += 4 * count;
        fulfilled += count;
      }
    }


    void setPad (GstPad * pad)
    {
      sinkpad = pad;
    }
    void setSrcPad (GstPad * pad)
    {
      srcpad = pad;
    }
  };


  class ArtsStereoSrc_impl:virtual public ArtsStereoSrc_skel,
      virtual public StdSynthModule
  {

    GstPad *srcpad;
    GstBuffer *outbuf;
    unsigned char *dataptr;

  public:

    void calculateBlock (unsigned long samples)
    {
//gint16 *s;
//fprintf(stderr,"StereoSrc: handed %d samples\n",samples);
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = (guchar *) g_malloc (samples * 4);
      GST_BUFFER_SIZE (outbuf) = samples * 4;
      memset (GST_BUFFER_DATA (outbuf), 0, samples * 4);
      convert_stereo_2float_i16le (samples, inleft, inright,
          GST_BUFFER_DATA (outbuf));
//s = (gint16 *)GST_BUFFER_DATA(outbuf);
//fprintf(stderr,"samples in are %f and %f, out are %d and %d\n",inleft[0],inright[0],s[0],s[1]);
      gst_pad_push (srcpad, GST_DATA (outbuf));
      outbuf = NULL;
    }


    void setPad (GstPad * pad)
    {
      srcpad = pad;
    }
  };

  class GstArtsWrapper
  {
    Dispatcher *dispatcher;
    ArtsStereoSink sink;
    ArtsStereoSrc source;
    StereoVolumeControl effect;

  public:
      GstArtsWrapper (GstPad * sinkpad, GstPad * sourcepad)
    {
      dispatcher = new Arts::Dispatcher ();
      ArtsStereoSink_impl *sink_impl = new ArtsStereoSink_impl ();
      ArtsStereoSrc_impl *source_impl = new ArtsStereoSrc_impl ();
        sink_impl->setPad (sinkpad);
        sink_impl->setSrcPad (sourcepad);
        source_impl->setPad (sourcepad);
        sink = ArtsStereoSink::_from_base (sink_impl);
        source = ArtsStereoSrc::_from_base (source_impl);
        sink.start ();
        effect.start ();
        source.start ();
        effect.scaleFactor (0.5);
        connect (sink, effect);
        connect (effect, source);
//    connect(sink,source);
    }
    void iterate ()
    {
      source._node ()->requireFlow ();
    }
  };


};


extern "C"
{

  void *gst_arts_wrapper_new (GstPad * sinkpad, GstPad * sourcepad)
  {
    return new Gst::GstArtsWrapper (sinkpad, sourcepad);
  }

  void gst_arts_wrapper_free (void *wrapper)
  {
    Gst::GstArtsWrapper * w = (Gst::GstArtsWrapper *) wrapper;
    delete w;
  }

  void gst_arts_wrapper_do (void *wrapper)
  {
    Gst::GstArtsWrapper * w = (Gst::GstArtsWrapper *) wrapper;
    w->iterate ();
  }

}

// vim:sts=2:sw=2
