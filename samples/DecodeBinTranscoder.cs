//
// DecodeBinTranscoder.cs: sample transcoder using DecodeBin binding
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using Gst;
using Gst.CorePlugins;
using Gst.BasePlugins;

public delegate void ErrorHandler (object o, ErrorArgs args);
public delegate void ProgressHandler (object o, ProgressArgs args);

public class ErrorArgs : EventArgs {
  public string Error;
}

public class ProgressArgs : EventArgs {
  public long Duration;
  public long Position;
}

public class DecodeBinTranscoder : IDisposable {
  private Pipeline pipeline;
  private FileSrc filesrc;
  private FileSink filesink;
  private Element audioconvert;
  private Element encoder;
  private DecodeBin2 decodebin;

  private uint progress_timeout;

  public event EventHandler Finished;
  public event ErrorHandler Error;
  public event ProgressHandler Progress;

  public DecodeBinTranscoder() {
    ConstructPipeline();
  }

  public void Transcode (string inputFile, string outputFile) {
    filesrc.Location = inputFile;
    filesink.Location = outputFile;

    pipeline.SetState (State.Playing);
    progress_timeout = GLib.Timeout.Add (250, OnProgressTimeout);
  }

  public void Dispose() {
    pipeline.Dispose();
  }

  protected virtual void OnFinished() {
    EventHandler handler = Finished;
    if (handler != null) {
      handler (this, new EventArgs());
    }
  }

  protected virtual void OnError (string error) {
    ErrorHandler handler = Error;
    if (handler != null) {
      ErrorArgs args = new ErrorArgs();
      args.Error = error;
      handler (this, args);
    }
  }

  protected virtual void OnProgress (long position, long duration) {
    ProgressHandler handler = Progress;
    if (handler != null) {
      ProgressArgs args = new ProgressArgs();
      args.Position = position;
      args.Duration = duration;
      handler (this, args);
    }
  }

  private void ConstructPipeline() {
    pipeline = new Pipeline ("pipeline");

    filesrc = ElementFactory.Make ("filesrc", "filesrc") as FileSrc;
    filesink = ElementFactory.Make ("filesink", "filesink") as FileSink;
    audioconvert = ElementFactory.Make ("audioconvert", "audioconvert");
    encoder = ElementFactory.Make ("wavenc", "wavenc");
    decodebin = ElementFactory.Make ("decodebin2", "decodebin") as DecodeBin2;
    decodebin.NewDecodedPad += OnNewDecodedPad;

    pipeline.Add (filesrc, decodebin, audioconvert, encoder, filesink);

    filesrc.Link (decodebin);
    audioconvert.Link (encoder);
    encoder.Link (filesink);

    pipeline.Bus.AddWatch (new BusFunc (OnBusMessage));
  }

  private void OnNewDecodedPad (object o, DecodeBin2.NewDecodedPadArgs args) {
    Pad sinkpad = audioconvert.GetStaticPad ("sink");

    if (sinkpad.IsLinked) {
      return;
    }

    Caps caps = args.Pad.Caps;
    Structure structure = caps[0];

    if (!structure.Name.StartsWith ("audio")) {
      return;
    }

    args.Pad.Link (sinkpad);
  }

  private bool OnBusMessage (Bus bus, Message message) {
    switch (message.Type) {
      case MessageType.Error:
        string msg;
        Enum err;
        message.ParseError (out err, out msg);
        GLib.Source.Remove (progress_timeout);
        OnError (msg);
        break;
      case MessageType.Eos:
        pipeline.SetState (State.Null);
        GLib.Source.Remove (progress_timeout);
        OnFinished();
        break;
    }

    return true;
  }

  private bool OnProgressTimeout() {
    long duration, position;
    Gst.Format fmt = Gst.Format.Time;

    if (pipeline.QueryDuration (ref fmt, out duration) && fmt == Gst.Format.Time && encoder.QueryPosition (ref fmt, out position) && fmt == Gst.Format.Time) {
      OnProgress (position, duration);
    }

    return true;
  }

  private static GLib.MainLoop loop;

  public static void Main (string [] args) {
    if (args.Length < 2) {
      Console.WriteLine ("Usage: mono decodebin-transcoder.exe <input-file> <output-file>");
      return;
    }

    Gst.Application.Init();
    loop = new GLib.MainLoop();

    DecodeBinTranscoder transcoder = new DecodeBinTranscoder();

    transcoder.Error += delegate (object o, ErrorArgs eargs) {
      Console.WriteLine ("Error: {0}", eargs.Error);
      transcoder.Dispose();
      loop.Quit();
    };

    transcoder.Finished += delegate {
      Console.WriteLine ("\nFinished");
      transcoder.Dispose();
      loop.Quit();
    };

    transcoder.Progress += delegate (object o, ProgressArgs pargs) {
      Console.Write ("\rEncoding: {0} / {1} ({2:00.00}%) ",
                     new TimeSpan ( (pargs.Position / (long) Clock.Second) * TimeSpan.TicksPerSecond),
                     new TimeSpan ( (pargs.Duration / (long) Clock.Second) * TimeSpan.TicksPerSecond),
                     ( (double) pargs.Position / (double) pargs.Duration) * 100.0);
    };

    transcoder.Transcode (args[0], args[1]);

    loop.Run();
  }
}
