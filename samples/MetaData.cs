// Authors
//   Copyright (C) 2006 Khaled Mohammed <khaled.mohammed@gmail.com>
//   Copyright (C) 2008 Paul Burton <paulburton89@gmail.com>

using System;
using System.IO;

using Gst;

public class MetaData {
  static Element pipeline = null;
  static Element source = null;

  static void PrintTag (TagList list, string tag) {
    uint count = list.GetTagSize (tag);

    //Console.WriteLine ("Tags found = " + count);

    for (uint i = 0; i < count; i++) {
      string str;

      try {
        str = list[tag, i].ToString ();
      } catch (Exception ex) {
        str = ex.Message;
      }

      if (i == 0)
        Console.WriteLine ("{0}: {1}", Tag.GetNick (tag).PadRight (25), str);
      else
        Console.WriteLine ("{0}{1}", string.Empty.PadRight (27), str);
    }
  }

  static bool MessageLoop (Element element, ref TagList tags) {
    Bus bus = element.Bus;
    bool done = false;

    while (!done) {
      Message message = bus.Pop ();

      if (message == null)
        break;

      switch (message.Type) {
        case MessageType.Error:
          Enum error;
          string msg;
          message.ParseError (out error, out msg);
          message.Dispose ();
          return true;

        case MessageType.Eos:
          message.Dispose ();
          return true;

        case MessageType.Tag:
          TagList new_tags;

          message.ParseTag (out new_tags);

          if (tags != null) {
            tags = tags.Merge (new_tags, TagMergeMode.KeepAll);
            new_tags.Dispose ();
          } else
            tags = new_tags;

          break;

        default:
          break;
      }

      message.Dispose ();
    }

    bus.Dispose ();
    return true;
  }

  static void MakePipeline () {
    Element decodebin;

    if (pipeline != null)
      pipeline.Dispose ();

    pipeline = new Pipeline (String.Empty);
    source = ElementFactory.Make ("filesrc", "source");
    decodebin = ElementFactory.Make ("decodebin", "decodebin");

    if (pipeline == null)
      Console.WriteLine ("Pipeline could not be created");
    if (source == null)
      Console.WriteLine ("Element filesrc could not be created");
    if (decodebin == null)
      Console.WriteLine ("Element decodebin could not be created");

    Bin bin = (Bin) pipeline;
    bin.Add (source, decodebin);

    if (!source.Link (decodebin))
      Console.WriteLine ("filesrc could not be linked with decodebin");

    //decodebin.Dispose ();
  }

  public static void Main (string [] args) {
    Application.Init ();

    if (args.Length < 1) {
      Console.WriteLine ("Please give filenames to read metadata from\n\n");
      return;
    }

    MakePipeline ();

    int i = -1;
    while (++i < args.Length) {
      State state, pending;
      TagList tags = null;

      string filename = args[i];

      if (!File.Exists (filename)) {
        Console.WriteLine ("File {0} does not exist", filename);
        continue;
      }

      source["location"] = filename;

      StateChangeReturn sret = pipeline.SetState (State.Paused);

      if (sret == StateChangeReturn.Async) {
        if (StateChangeReturn.Success != pipeline.GetState (out state, out pending, Clock.Second * 5)) {
          Console.WriteLine ("State change failed for {0}. Aborting\n", filename);
          break;
        }
      } else if (sret != StateChangeReturn.Success) {
        Console.WriteLine ("{0} - Could not read file ({1})\n", filename, sret);
        continue;
      }

      if (!MessageLoop (pipeline, ref tags))
        Console.Error.WriteLine ("Failed in message reading for {0}", args[i]);

      if (tags != null) {
        Console.WriteLine ("Metadata for {0}:", filename);

        foreach (string tag in tags.Tags)
          PrintTag (tags, tag);
        tags.Dispose ();
        tags = null;
      } else
        Console.WriteLine ("No metadata found for {0}", args[0]);

      sret = pipeline.SetState (State.Null);

      if (StateChangeReturn.Async == sret) {
        if (StateChangeReturn.Failure == pipeline.GetState (out state, out pending, Clock.TimeNone))
          Console.WriteLine ("State change failed. Aborting");
      }
    }

    if (pipeline != null)
      pipeline.Dispose ();
  }
}
