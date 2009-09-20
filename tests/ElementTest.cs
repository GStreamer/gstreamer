//
// ElementTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;
using Gst;
using Gst.GLib;

[TestFixture]
public class ElementTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestLinkNoPads() {
    Element src = new Bin ("src");
    Element sink = new Bin ("sink");

    Assert.IsFalse (src.Link (sink));
    Assert.IsFalse (Element.Link (src, sink));
  }

  public class PadAddElement : Gst.Element {
    public PadAddElement () : base () {
      Pad pad = new Pad ("source", PadDirection.Src);
      CollectionAssert.IsEmpty (Pads);

      AddPad (pad);
      Assert.AreEqual (pad, GetStaticPad ("source"));
      CollectionAssert.Contains (Pads, pad);

      RemovePad (pad);
      Assert.IsNull (GetStaticPad ("source"));
      CollectionAssert.IsEmpty (Pads);
    }
  }

  [Test]
  public void TestAddRemovePad() {
    new PadAddElement ();
  }

  [Test]
  public void TestLink() {
    State state, pending;

    Element source = ElementFactory.Make ("fakesrc", "source");
    Assert.IsNotNull (source);
    Element sink = ElementFactory.Make ("fakesink", "sink");
    Assert.IsNotNull (sink);

    Assert.IsTrue (source.LinkPads ("src", sink, "sink"));

    sink.SetState (State.Paused);
    source.SetState (State.Paused);
    sink.GetState (out state, out pending, Clock.TimeNone);
    Assert.AreEqual (state, State.Paused);

    sink.SetState (State.Playing);
    source.SetState (State.Playing);
    source.GetState (out state, out pending, Clock.TimeNone);
    Assert.AreEqual (state, State.Playing);

    sink.SetState (State.Null);
    source.SetState (State.Null);
    sink.GetState (out state, out pending, Clock.TimeNone);
    Assert.AreEqual (state, State.Null);

    Assert.AreEqual (source.GetStaticPad ("src").Peer, sink.GetStaticPad ("sink"));
    source.Unlink (sink);
    Assert.IsFalse (source.GetStaticPad ("src").IsLinked);
  }

  public class TestSubclassesApp {
    static MainLoop loop;

    public class MySrc : Gst.Element {
      public MySrc () : base () {
        Init ();
      }

      public MySrc (IntPtr raw) : base (raw) {
        Init ();
      }

      private Pad src;
      private uint nbuffers = 0;

      private void Init () {
        src = new Pad (templ, "src");
        AddPad (src);
      }

      static Caps caps = Caps.FromString ("my/dummy-data");

      private void loop () {
        Gst.Buffer buf = new Gst.Buffer ();
        buf.Caps = caps;
        Gst.FlowReturn ret = src.Push (buf);
        nbuffers++;

        Assert.AreEqual (ret, Gst.FlowReturn.Ok);
        if (ret != Gst.FlowReturn.Ok) {
          src.StopTask ();
          this.PostMessage (Message.NewError (this, CoreError.Failed, "Oh no"));
        }

        if (nbuffers == 10) {
          Assert.IsTrue (src.PushEvent (Gst.Event.NewEos ()));
          src.PauseTask ();
        }
      }

      protected override StateChangeReturn OnChangeState (StateChange transition) {
        if (transition == StateChange.ReadyToPaused)
          src.StartTask (loop);
        else if (transition == StateChange.PausedToReady)
          src.StopTask ();

        return StateChangeReturn.Success;
      }

      static PadTemplate templ = new PadTemplate ("src", Gst.PadDirection.Src, Gst.PadPresence.Always, Caps.FromString ("my/dummy-data"));

      public static bool Register () {
        SetDetails ( (GType) typeof (MySrc), "long", "klass", "desc", "author");
        AddPadTemplate ( (GType) typeof (MySrc), templ);
        return ElementFactory.Register (null, "mysrc", (uint) Gst.Rank.None, (GType) typeof (MySrc));
      }
    }

    public class MySink : Gst.Element {
      public MySink () : base () {
        Init ();
      }

      public MySink (IntPtr raw) : base (raw) {
        Init ();
      }

      Gst.FlowReturn on_chain (Gst.Pad pad, Gst.Buffer buffer) {
        Assert.IsNotNull (buffer);
        return Gst.FlowReturn.Ok;
      }

      bool on_event (Gst.Pad pad, Gst.Event evnt) {
        if (evnt.Type == Gst.EventType.Eos) {
          this.PostMessage (Message.NewEos (this));
        }

        return true;
      }

      private void Init () {
        Pad pad = new Pad (templ, "sink");
        pad.ChainFunction = on_chain;
        pad.EventFunction = on_event;
        AddPad (pad);
      }

      static PadTemplate templ = new PadTemplate ("sink", Gst.PadDirection.Sink, Gst.PadPresence.Always, Caps.FromString ("my/dummy-data"));

      public static bool Register () {
        SetDetails ( (GType) typeof (MySink), "long", "klass", "desc", "author");
        AddPadTemplate ( (GType) typeof (MySink), templ);

        return ElementFactory.Register (null, "mysink", (uint) Gst.Rank.None, (GType) typeof (MySink));
      }
    }

    private static bool BusCb (Bus bus, Message message) {
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
    }

    public static void Run () {

      MySrc.Register ();
      MySink.Register ();

      MySrc mysrc = Gst.ElementFactory.Make ("mysrc") as MySrc;
      MySink mysink = Gst.ElementFactory.Make ("mysink") as MySink;

      Gst.Pipeline pipeline = new Pipeline ("pipeline");
      pipeline.Add (mysrc, mysink);
      Assert.IsTrue (mysrc.Link (mysink));

      loop = new MainLoop ();

      pipeline.Bus.AddWatch (new BusFunc (BusCb));
      pipeline.SetState (Gst.State.Playing);

      loop.Run ();

      pipeline.SetState (Gst.State.Null);
    }
  }

  [Test]
  public void TestSubclasses () {
    TestSubclassesApp.Run ();
  }
}
