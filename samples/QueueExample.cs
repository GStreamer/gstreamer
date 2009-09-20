//
// Authors:
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
//

using Gst;
using System;

public class QueueExample {
  public static void Main (string [] args) {
    Application.Init();

    if (args.Length != 1) {
      Console.Error.WriteLine ("usage: mono queueexample.exe <filename>\n");
      return;
    }

    Element pipeline = new Pipeline ("pipeline");

    Element filesrc = ElementFactory.Make ("filesrc", "disk_source");
    filesrc.SetProperty ("location", args[0]);
    Element decode = ElementFactory.Make ("mad", "decode");
    Element queue = ElementFactory.Make ("queue", "queue");

    Element audiosink = ElementFactory.Make ("alsasink", "play_audio");

    Bin bin = (Bin) pipeline;
    bin.AddMany (filesrc, decode, queue, audiosink);

    Element.LinkMany (filesrc, decode, queue, audiosink);

    pipeline.SetState (State.Playing);

    EventLoop (pipeline);

    pipeline.SetState (State.Null);
  }

  static void EventLoop (Element pipe) {
    Bus bus = pipe.Bus;

    while (true) {
      Message message = bus.Poll (MessageType.Any, -1);

      switch (message.Type) {
        case MessageType.Eos: {
          message.Dispose();
          return;
        }
        case MessageType.Error: {
          return;
        }
      }
    }
  }
}
