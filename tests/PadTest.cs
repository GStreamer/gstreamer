//
// PadTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Michael Dominic K. (michaldominik@gmail.com)
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;
using Gst;

[TestFixture]
public class PadTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestPlainCreation() {
    Pad src = new Pad ("src", PadDirection.Src);
    Pad sink = new Pad ("sink", PadDirection.Sink);

    Assert.IsNotNull (src);
    Assert.IsNotNull (sink);

    Assert.IsFalse (src.Handle == IntPtr.Zero, "Ooops, src pad has null handle");
    Assert.IsFalse (sink.Handle == IntPtr.Zero, "Ooops, sink pad has null handle");

    Assert.AreEqual (PadDirection.Src, src.Direction);
    Assert.AreEqual (PadDirection.Sink, sink.Direction);
  }

  public static Caps PadGetCapsStub (Pad pad) {
    return Caps.FromString ("video/x-raw-yuv");
  }

  [Test]
  public void TestFuncAssigning() {
    Pad src = new Pad ("src", PadDirection.Src);
    src.GetCapsFunction = new PadGetCapsFunction (PadGetCapsStub);

    Caps caps = src.Caps;

    Assert.IsNotNull (caps, "Ooops, returned caps is null");
    Assert.IsFalse (caps.IsEmpty == true, "Ooops, returned caps are empty");
    Assert.AreEqual ("video/x-raw-yuv", caps.ToString ());
  }

  [Test]
  public void TestElementPadAccessByName() {
    Element element = ElementFactory.Make ("identity");
    Assert.IsNotNull (element);
    Assert.IsFalse (element.Handle == IntPtr.Zero, "Ooops, identity element has null handle");

    Pad src = element.GetStaticPad ("src");
    Pad sink = element.GetStaticPad ("sink");

    Assert.IsNotNull (src, "Ooops, src pad is null");
    Assert.IsNotNull (sink, "Ooops, sink pad is null");

    Assert.IsFalse (src.Handle == IntPtr.Zero, "Ooops, src pad has null handle");
    Assert.IsFalse (sink.Handle == IntPtr.Zero, "Ooops, sink pad has null handle");

    Caps srccaps = src.Caps;
    Assert.IsTrue (srccaps.IsAny, "How come src pad caps is not ANY?");

    Caps sinkcaps = sink.Caps;
    Assert.IsTrue (sinkcaps.IsAny, "How come sink pad caps is not ANY?");
  }

  [Test]
  public void TestElementPadAccessByList() {
    Element element = ElementFactory.Make ("identity");
    Assert.IsNotNull (element);
    Assert.IsFalse (element.Handle == IntPtr.Zero, "Ooops, identity element has null handle");

    bool hassink = false;
    bool hassrc = false;

    foreach (Pad pad in element.Pads) {
      if (pad.Name == "src")
        hassrc = true;
      else if (pad.Name == "sink")
        hassink = true;
    }

    Assert.IsTrue (hassink, "Sink pad not found in the list");
    Assert.IsTrue (hassrc, "Src pad not found in the list");
  }

  [Test]
  public void TestLink() {
    Pad src = new Pad ("source", PadDirection.Src);
    Assert.IsNotNull (src, "Pad could not be created");

    string name = src.Name;
    Assert.AreEqual (name, "source");

    Pad sink = new Pad ("sink", PadDirection.Sink);
    Assert.IsNotNull (sink, "Pad could not be created");

    Assert.AreEqual (src.Link (sink), PadLinkReturn.Noformat);
  }

  [Test]
  public void TestGetAllowedCaps() {
    Caps caps;

    Pad sink = new Pad ("sink", PadDirection.Sink);
    caps = sink.AllowedCaps;
    Assert.IsNull (caps);

    Pad src = new Pad ("src", PadDirection.Src);
    caps = src.AllowedCaps;
    Assert.IsNull (caps);

    caps = Caps.FromString ("foo/bar");

    src.SetCaps (caps);
    sink.SetCaps (caps);

    PadLinkReturn plr = src.Link (sink);
    Assert.AreEqual (plr, PadLinkReturn.Ok);

    Caps gotcaps = src.AllowedCaps;
    Assert.IsNotNull (gotcaps);
    Assert.IsTrue (gotcaps.IsEqual (caps));
  }

  bool ProbeHandler (Pad pad, Gst.Buffer buffer) {
    //Console.WriteLine("event worked");
    return false;
  }

  [Test]
  public void TestPushUnlinked() {
    Pad src = new Pad ("src", PadDirection.Src);
    Assert.IsNotNull (src, "Could not create src");
    Caps caps = src.AllowedCaps;
    Assert.IsNull (caps);

    caps = Caps.FromString ("foo/bar");
    src.SetCaps (caps);

    Gst.Buffer buffer = new Gst.Buffer();
    Assert.AreEqual (src.Push (buffer), FlowReturn.NotLinked);

    ulong handler_id = src.AddBufferProbe (new PadBufferProbeCallback (ProbeHandler));
    buffer = new Gst.Buffer (new byte[] {0});
    FlowReturn flowreturn = src.Push (buffer);
    Assert.AreEqual (flowreturn, FlowReturn.Ok);
  }
}
