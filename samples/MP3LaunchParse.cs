//
// Authors
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using System;
using Gst;


public class MP3LaunchParse {
  static void EventLoop (Element pipe) {
    Bus bus = pipe.Bus;
    Message message = null;

    while (true) {
      message = bus.Poll (MessageType.Any, -1);

      if (message == null) {
        Console.Error.WriteLine ("Message is null!!!");
        System.Application.Exit();
      }

      switch (message.Type) {
        case MessageType.Eos:
          message.Dispose();
          return;
        case MessageType.Warning:
        case MessageType.Error:
          message.Dispose();
          return;
        default:
          message.Dispose();
          break;
      }
    }
  }

  public static void Main (string [] args) {
    Application.Init();

    if (args.Length != 1) {
      Console.Error.WriteLine ("usage: mono mp3launchparse.exe <mp3 file>\n", args[0]);
      return;
    }

    Element bin = (Element) Parse.Launch ("filesrc name=my_filesrc ! mad ! osssink", &error);
    if (!bin) {
      Console.Error.WriteLine ("Parse error");
      Application.Exit();
    }

    Bin b = (Bin) bin;

    Element filesrc = b.GetByName ("my_filesrc");
    filesrc.SetProperty ("location", args[0]);

    bin.SetState (State.Playing);

    EventLoop (bin);

    bin.SetState (State.Null);
    return;
  }
}
