//
// PipelineTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors
//   Khaled Mohammed < khaled.mohammed@gmail.com >
//
// (C) 2006
//

using System;
using NUnit.Framework;
using Gst;
using Gst.CorePlugins;

[TestFixture]
public class PipelineTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestPipeline() {
    Pipeline pipeline = new Pipeline();
    Assert.IsNotNull (pipeline, "Could not create pipeline");
    Assert.IsNotNull (pipeline.Bus, "Bus on pipeline is null");
    Assert.IsNotNull (pipeline.Clock, "Clock on pipeline is null");
  }

  [Test]
  public void TestAsyncStateChangeEmpty() {
    Pipeline pipeline = new Pipeline();
    Assert.IsNotNull (pipeline, "Could not create pipeline");

    Assert.AreEqual ( ( (Element) pipeline).SetState (State.Playing), StateChangeReturn.Success);
  }

  [Test]
  public void TestAsyncStateChangeFakeReady() {
    Pipeline pipeline = new Pipeline();
    Element src = ElementFactory.Make ("fakesrc");
    Element sink = ElementFactory.Make ("fakesink");

    Bin bin = (Bin) pipeline;
    bin.Add (src, sink);
    src.Link (sink);

    Assert.AreEqual ( ( (Element) pipeline).SetState (State.Ready), StateChangeReturn.Success);
  }

  [Test]
  public void TestAsyncStateChangeFake() {
    bool done = false;
    Pipeline pipeline = new Pipeline();
    Assert.IsNotNull (pipeline, "Could not create pipeline");

    Element src = ElementFactory.Make ("fakesrc");
    Element sink = ElementFactory.Make ("fakesink");

    Bin bin = (Bin) pipeline;
    bin.Add (src, sink);
    src.Link (sink);

    Bus bus = pipeline.Bus;

    Assert.AreEqual ( ( (Element) pipeline).SetState (State.Playing), StateChangeReturn.Async);

    while (!done) {
      State old, newState, pending;
      Message message = bus.Poll (MessageType.StateChanged, -1);
      if (message != null) {
        message.ParseStateChanged (out old, out newState, out pending);
        if (message.Src == (Gst.Object) pipeline && newState == State.Playing)
          done = true;
      }
    }

    Assert.AreEqual ( ( (Element) pipeline).SetState (State.Null), StateChangeReturn.Success);
  }

  Element pipeline;
  Gst.GLib.MainLoop loop;

  bool MessageReceived (Bus bus, Message message) {
    MessageType type = message.Type;

    switch (type) {
      case MessageType.StateChanged: {
        State old, newState, pending;
        message.ParseStateChanged (out old, out newState, out pending);
        if (message.Src == (Gst.Object) pipeline && newState == State.Playing) {
          loop.Quit();
        }
        break;
      }
      case MessageType.Error:
        break;
      default:
        break;
    }
    return true;
  }

  [Test]
  public void TestBusAddWatch() {
    TestBusCallback (true);
  }

  [Test]
  public void TestBusAddSignalWatch() {
    TestBusCallback (false);
  }

  public void TestBusCallback (bool use_AddWatch) {
    pipeline = new Pipeline();
    Assert.IsNotNull (pipeline, "Could not create pipeline");

    Element src = ElementFactory.Make ("fakesrc");
    Assert.IsNotNull (src, "Could not create fakesrc");
    Element sink = ElementFactory.Make ("fakesink");
    Assert.IsNotNull (sink, "Could not create fakesink");

    Bin bin = (Bin) pipeline;
    bin.Add (src, sink);
    Assert.IsTrue (src.Link (sink), "Could not link between src and sink");

    if (use_AddWatch)
      pipeline.Bus.AddWatch (new BusFunc (MessageReceived));
    else {
      pipeline.Bus.AddSignalWatch();
      pipeline.Bus.Message += delegate (object o, MessageArgs args) {
        MessageReceived (null, args.Message);
      };
    }
    Assert.AreEqual (pipeline.SetState (State.Playing), StateChangeReturn.Async);

    loop = new Gst.GLib.MainLoop();
    loop.Run();

    Assert.AreEqual (pipeline.SetState (State.Null), StateChangeReturn.Success);
    State current, pending;
    Assert.AreEqual (pipeline.GetState (out current, out pending, Clock.TimeNone), StateChangeReturn.Success);
    Assert.AreEqual (current, State.Null, "state is not NULL but " + current);
  }

  [Test]
  public void TestBaseTime() {
    Element pipeline = ElementFactory.Make ("pipeline", "pipeline");
    FakeSrc fakesrc = FakeSrc.Make ("fakesrc");
    FakeSink fakesink = FakeSink.Make ("fakesink");

    Assert.IsNotNull (pipeline, "Could not create pipeline");
    Assert.IsNotNull (fakesrc, "Could not create fakesrc");
    Assert.IsNotNull (fakesink, "Could not create fakesink");

    fakesrc.IsLive = true;

    Bin bin = (Bin) pipeline;
    bin.Add (fakesrc, fakesink);
    Assert.IsTrue (fakesrc.Link (fakesink));

    Pad sink = fakesink.GetStaticPad ("sink");
  }
}
