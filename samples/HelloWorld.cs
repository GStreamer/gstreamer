using System;
using Gst;
using GLib;

public class HelloWorld {
  private MainLoop loop;
  private Element pipeline, source, parser, decoder, conv, identity, sink;

  public static void Main (string [] args) {
    new HelloWorld (args);
  }

  public HelloWorld (string [] args) {
    Application.Init();

    loop = new MainLoop();
    pipeline = new Pipeline ("audio-player");

    if ( (source = ElementFactory.Make ("filesrc", "file-source")) == null) {
      Console.WriteLine ("Could not create file-source");
    }

    parser = ElementFactory.Make ("oggdemux", "ogg-parser");
    decoder = ElementFactory.Make ("vorbisdec", "vorbis-decoder");
    conv = ElementFactory.Make ("audioconvert", "converter");
    identity = ElementFactory.Make ("identity", "identitye");
    sink = ElementFactory.Make ("alsasink", "alsa-output");

    source["location"] = args[0];

    Bin bin = (Bin) pipeline;
    bin.Bus.AddWatch (new BusFunc (BusCall));

    bin.Add (source, parser, decoder, conv, identity, sink);

    if (!source.Link (parser)) {
      Console.WriteLine ("link failed between source and parser");
    }

    if (!decoder.Link (conv)) {
      Console.WriteLine ("link failed between decoder and converter");
    }

    if (!conv.Link (identity)) {
      Console.WriteLine ("link failed between converter and identity");
    }

    if (!identity.Link (sink)) {
      Console.Error.WriteLine ("link failed between identity and sink");
    }

    parser.PadAdded += new PadAddedHandler (OnPadAdded);
    identity.Connect ("handoff", OnHandoff);

    pipeline.SetState (State.Playing);

    Console.WriteLine ("Playing [" + args[0] + "]");

    loop.Run();

    pipeline.SetState (State.Null);
    pipeline.Dispose();
  }

  private bool BusCall (Bus bus, Message message) {
    switch (message.Type) {
      case MessageType.Error:
        string msg;
        Enum err;
        message.ParseError (out err, out msg);
        Console.WriteLine ("Gstreamer error: {0}", msg);
        loop.Quit();
        break;
      case MessageType.Eos:
        Console.WriteLine ("End-of-stream");
        loop.Quit();
        break;
      default:
        Console.WriteLine ("Entered BusCall:\t{0}", message.Type);
        break;
    }

    return true;
  }

  private void OnHandoff (object o, Gst.GLib.SignalArgs args) {
    Gst.Buffer buffer = args.Args[0] as Gst.Buffer;
    Console.WriteLine (buffer.Duration + "\t" + buffer.Timestamp);
  }

  void OnPadAdded (object o, PadAddedArgs args) {
    Console.WriteLine ("Entered OnPadAdded");
    Pad sinkpad = decoder.GetStaticPad ("sink");
    args.Pad.Link (sinkpad);
  }
}
