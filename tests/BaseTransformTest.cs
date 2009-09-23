using System;
using NUnit.Framework;
using Gst;
using Gst.Base;

[TestFixture]
public class BaseTransformTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  private class MyTransformIp : Gst.Base.BaseTransform {

    public static bool Register () {
      Gst.GLib.GType gtype = (Gst.GLib.GType) typeof (MyTransformIp);
      SetDetails (gtype, "My Transform", "Filter/Transform", "Do nothing useful", "Nobody");

      Caps caps = Caps.FromString ("foo/bar");

      AddPadTemplate (gtype, new PadTemplate ("src", PadDirection.Src, PadPresence.Always, caps));
      AddPadTemplate (gtype, new PadTemplate ("sink", PadDirection.Sink, PadPresence.Always, caps));
      return ElementFactory.Register (null, "mytransform-ip", (uint) Gst.Rank.None, gtype);
    }

    protected override FlowReturn OnTransformIp (Gst.Buffer buf) {
      Assert.IsTrue (buf.IsWritable);
      return base.OnTransformIp (buf);
    }
  }

  [Test]
  public void TestBufferOwnership () {
    MyTransformIp.Register ();

    Pipeline pipeline = new Pipeline ();
    Element src = ElementFactory.Make ("fakesrc");
    src["num-buffers"] = 10;
    Element transform = new MyTransformIp ();
    Element sink = ElementFactory.Make ("fakesink");

    pipeline.Add (src, transform, sink);
    Element.Link (src, transform, sink);

    Gst.GLib.MainLoop loop = new Gst.GLib.MainLoop ();

    pipeline.Bus.AddWatch (delegate (Bus bus, Message message) {
                             switch (message.Type) {
                             case MessageType.Error:
                                 Enum err;
                                 string msg;

                                 message.ParseError (out err, out msg);
                                 Assert.Fail (String.Format ("Error message: {0}", msg));
                                 loop.Quit ();
                                 break;
                               case MessageType.Eos:
                                   loop.Quit ();
                                   break;
                                 }
                                 return true;
                               });

    pipeline.SetState (State.Playing);
    loop.Run ();
    pipeline.SetState (State.Null);
  }

  private class MyTransformNIp : Gst.Base.BaseTransform {
    public bool transformed = false;

    public static bool Register () {
      Gst.GLib.GType gtype = (Gst.GLib.GType) typeof (MyTransformNIp);
      SetDetails (gtype, "My Transform", "Filter/Transform", "Do nothing useful", "Nobody");

      Caps caps = Caps.FromString ("foo/bar");

      AddPadTemplate (gtype, new PadTemplate ("src", PadDirection.Src, PadPresence.Always, caps));
      AddPadTemplate (gtype, new PadTemplate ("sink", PadDirection.Sink, PadPresence.Always, caps));
      return ElementFactory.Register (null, "mytransform-nip", (uint) Gst.Rank.None, gtype);
    }

    protected override FlowReturn OnTransform (Gst.Buffer inbuf, Gst.Buffer outbuf) {
      Assert.IsTrue (outbuf.IsWritable);
      transformed = true;
      return base.OnTransform (inbuf, outbuf);
    }

    protected override bool OnSetCaps (Caps incaps, Caps outcaps) {
      Assert.IsTrue (incaps.IsEqual (outcaps));
      return base.OnSetCaps (incaps, outcaps);
    }

    protected override bool OnTransformSize (Gst.PadDirection direction, Gst.Caps caps, uint size, Gst.Caps othercaps, out uint othersize) {
      othersize = size;
      return true;
    }
  }

  [Test]
  public void TestBufferOwnershipNIp () {
    MyTransformNIp.Register ();

    Pipeline pipeline = new Pipeline ();
    Element src = ElementFactory.Make ("fakesrc");
    src["sizetype"] = 2;
    Element capsfilter = ElementFactory.Make ("capsfilter");
    capsfilter["caps"] = Caps.FromString ("foo/bar");
    src["num-buffers"] = 10;
    MyTransformNIp transform = new MyTransformNIp ();
    Element sink = ElementFactory.Make ("fakesink");

    pipeline.Add (src, capsfilter, transform, sink);
    Element.Link (src, capsfilter, transform, sink);

    Gst.GLib.MainLoop loop = new Gst.GLib.MainLoop ();

    pipeline.Bus.AddWatch (delegate (Bus bus, Message message) {
                             switch (message.Type) {
                             case MessageType.Error:
                                 Enum err;
                                 string msg;

                                 message.ParseError (out err, out msg);
                                 Assert.Fail (String.Format ("Error message: {0}", msg));
                                 loop.Quit ();
                                 break;
                               case MessageType.Eos:
                                   loop.Quit ();
                                   break;
                                 }
                                 return true;
                               });

    pipeline.SetState (State.Playing);
    loop.Run ();
    Assert.IsTrue (transform.transformed);
    pipeline.SetState (State.Null);
  }
}
