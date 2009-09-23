using Gst;
using Gst.Video;
using Gst.Interfaces;
using Cairo;
using System;

public class TransformSample : Gst.Video.VideoFilter {
  private int lastX = -1, lastY = -1;
  private int width = 0, height = 0;

  static bool Register () {
    Gst.GLib.GType gtype = (Gst.GLib.GType) typeof (TransformSample);
    SetDetails (gtype, "Transform Sample", "Filter/Effect/Video",
                "Draws a moving line on top of a video stream and handles mouse clicks",
                "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

    Caps tmp = VideoUtil.FormatToTemplateCaps ( (BitConverter.IsLittleEndian) ? VideoFormat.BGRX : VideoFormat.XRGB);
    Caps caps = tmp.Copy ();
    caps[0]["width"] = 640;
    caps[0]["height"] = 480;
    caps.Append (tmp);

    AddPadTemplate (gtype, new PadTemplate ("src", PadDirection.Src, PadPresence.Always, caps));
    AddPadTemplate (gtype, new PadTemplate ("sink", PadDirection.Sink, PadPresence.Always, caps));
    return ElementFactory.Register (null, "transformsample", (uint) Gst.Rank.None, gtype);
  }

  protected override FlowReturn OnTransformIp (Gst.Buffer buf) {
    if (!buf.IsWritable)
      return FlowReturn.Error;

    Cairo.ImageSurface img = new Cairo.ImageSurface (buf.Data, Cairo.Format.Rgb24, width, height, width*4);

    using (Cairo.Context context = new Cairo.Context (img)) {
      double dx = (double) ( (buf.Timestamp / Clock.MSecond) % 2180) / 5;
      context.Save ();
      context.Scale (width / 640.0, height / 480.0);
      context.MoveTo (300, 10 + dx);
      context.LineTo (500 - dx, 400);
      context.LineWidth = 4.0;
      context.Color = new Color (0, 0, 1.0);
      context.Stroke();
      context.Restore ();

      if (lastX != -1 && lastY != -1) {
        context.Color = new Color (1.0, 0, 0);
        context.Translate (lastX, lastY);
        context.Scale (Math.Min (width / 640.0, height / 480.0), Math.Min (width / 640.0, height / 480.0));
        context.Arc (0, 0, 10.0, 0.0, 2 * Math.PI);
        context.Fill();
      }
    }

    img.Destroy ();
    return base.OnTransformIp (buf);
  }

  protected override bool OnSetCaps (Caps incaps, Caps outcaps) {
    width = (int) incaps[0]["width"];
    height = (int) incaps[0]["height"];
    return base.OnSetCaps (incaps, outcaps);
  }

  protected override bool OnSrcEvent (Gst.Event evt) {
    NavigationEventType t = NavigationEvent.EventGetType (evt);

    if (t == NavigationEventType.MouseButtonPress) {
      double x, y;
      int btn;

      if (NavigationEvent.ParseMouseButtonEvent (evt, out btn, out x, out y)) {
        if (btn == 1) {
          lastX = (int) x;
          lastY = (int) y;
        }
      }
    } else if (t == NavigationEventType.MouseButtonRelease) {
      lastX = lastY = -1;
    }

    return base.OnSrcEvent (evt);
  }

  static void Main (string[] args) {
    Gst.Application.Init ();
    TransformSample.Register ();

    Pipeline pipeline = new Pipeline ();
    Element videotestsrc = ElementFactory.Make ("videotestsrc");
    Element transform = new TransformSample ();
    Element ffmpegcolorspace = ElementFactory.Make ("ffmpegcolorspace");
    Element videosink = ElementFactory.Make ("autovideosink");

    pipeline.Add (videotestsrc, transform, ffmpegcolorspace, videosink);
    Element.Link (videotestsrc, transform, ffmpegcolorspace, videosink);

    Gst.GLib.MainLoop loop = new Gst.GLib.MainLoop ();

    pipeline.Bus.AddSignalWatch();
    pipeline.Bus.Message += delegate (object sender, MessageArgs margs) {
      Message message = margs.Message;

      switch (message.Type) {
        case MessageType.Error:
          Enum err;
          string msg;

          message.ParseError (out err, out msg);
          System.Console.WriteLine (String.Format ("Error message: {0}", msg));
          loop.Quit ();
          break;
        case MessageType.Eos:
          loop.Quit ();
          break;
      }
    };

    pipeline.SetState (State.Playing);
    loop.Run ();
    pipeline.SetState (State.Null);
  }
}
