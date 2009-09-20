using System;
using Gst;
using Gst.CorePlugins;

public static class GstTypefindTest {
  private static TypeFindElement typefind;

  public static void Main (string [] args) {
    Application.Init();

    Pipeline pipeline = new Pipeline ("pipeline");
    FileSrc source = FileSrc.Make ("source");
    typefind = TypeFindElement.Make ("typefind");
    FakeSink sink = FakeSink.Make ("sink");

    source.Location = args[0];

    typefind.HaveType += OnHaveType;

    pipeline.Add (source, typefind, sink);
    source.Link (typefind);
    typefind.Link (sink);

    pipeline.SetState (State.Paused);
    pipeline.SetState (State.Null);

    pipeline.Dispose();
  }

  private static void OnHaveType (object o, TypeFindElement.HaveTypeArgs args) {
    Console.WriteLine ("MimeType: {0}", args.Caps);
  }
}

