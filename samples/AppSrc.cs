/*
 * AppSrc.cs: example of using the appsrc element
 *
 * Authors:
 *   Maarten Bosmans <mkbosmans@gmail.com>
 *
 * Copyright (C) 2009 Maarten Bosmans
 *
 */

using System;
using System.Runtime.InteropServices;
using GLib;
using Gst;
using Cairo;

public class AppSrcDemo {
  static MainLoop loop;
  static Gst.App.AppSrc appsrc;
  static Pipeline pipeline;

  public static void Main (string[] args) {
    Application.Init();
    loop = new MainLoop();

    // Construct all the elements
    pipeline = new Pipeline();
    appsrc = new Gst.App.AppSrc ("AppSrcDemo");
    Element color = ElementFactory.Make ("ffmpegcolorspace");
    Element sink = ElementFactory.Make ("autovideosink");

    // Link the elements
    pipeline.Add (appsrc, color, sink);
    Element.Link (appsrc, color, sink);

    // Set the caps on the AppSrc to RGBA, 640x480, 4 fps, square pixels
    Gst.Video.VideoFormat fmt = (BitConverter.IsLittleEndian) ? Gst.Video.VideoFormat.BGRA : Gst.Video.VideoFormat.ARGB;
    appsrc.Caps = Gst.Video.VideoUtil.FormatNewCaps (fmt, 640, 480, 4, 1, 1, 1);

    // Connect the handlers
    appsrc.NeedData += PushAppData;
    pipeline.Bus.AddSignalWatch();
    pipeline.Bus.Message += MessageHandler;

    // Run, loop, run!
    pipeline.SetState (State.Playing);
    loop.Run();
    pipeline.SetState (State.Null);
  }

  static void PushAppData (object o, Gst.App.NeedDataArgs args) {
    ulong mseconds = 0;
    if (appsrc.Clock != null)
      mseconds = appsrc.Clock.Time / Clock.MSecond;
    Gst.Buffer buffer = DrawData (mseconds);
    appsrc.PushBuffer (buffer);
  }

  // Returns a Gst.Buffer presentation of one 640x480 BGRA frame using Cairo
  static Gst.Buffer DrawData (ulong seconds) {
    Gst.Buffer buffer = new Gst.Buffer (640*480*4);
    Cairo.ImageSurface img = new Cairo.ImageSurface (buffer.Data, Cairo.Format.Argb32, 640, 480, 640*4);
    using (Cairo.Context context = new Cairo.Context (img)) {
      double dx = (double) (seconds % 2180) / 5;
      context.Color = new Color (1.0, 1.0, 0);
      context.Paint();
      context.MoveTo (300, 10 + dx);
      context.LineTo (500 - dx, 400);
      context.LineWidth = 4.0;
      context.Color = new Color (0, 0, 1.0);
      context.Stroke();
    }
    img.Destroy();
    return buffer;
  }

  static void MessageHandler (object sender, MessageArgs args) {
    Message message = args.Message;
    string text = String.Format ("Message from {0}:  \t{1}", message.Src.Name, message.Type);
    switch (message.Type) {
      case MessageType.Error:
        Enum err;
        string msg;
        message.ParseError (out err, out msg);
        text += String.Format ("\t({0})", msg);
        break;
      case MessageType.StateChanged:
        State oldstate, newstate, pending;
        message.ParseStateChanged (out oldstate, out newstate, out pending);
        text += String.Format ("\t\t{0} -> {1}   ({2})", oldstate, newstate, pending);
        break;
      case MessageType.Eos:
        loop.Quit();
        break;
    }
    Console.WriteLine (text);
  }
}

