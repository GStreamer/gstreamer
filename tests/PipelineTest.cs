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

[TestFixture]
public class PipelineTest 
{
	[TestFixtureSetUp]
	public void Init() 
	{
		Application.Init();
	}

	[TestFixtureTearDown]
	public void Deinit()
	{
		Application.Deinit();
	}

	[Test]
	public void TestAsyncStateChangeEmpty()
	{
		Pipeline pipeline = new Pipeline(String.Empty);
		Assert.IsNotNull(pipeline, "Could not create pipeline");
		
		Assert.AreEqual(((Element)pipeline).SetState(State.Playing), StateChangeReturn.Success);

		pipeline.Dispose();
	}

	[Test]
	public void TestAsyncStateChangeFakeReady()
	{
		Pipeline pipeline = new Pipeline(String.Empty);
		Element src = ElementFactory.Make("fakesrc", null);
		Element sink = ElementFactory.Make("fakesink", null);

		Bin bin = (Bin) pipeline;
		bin.AddMany(src, sink);
		src.Link(sink);

		Assert.AreEqual(((Element)pipeline).SetState(State.Ready), StateChangeReturn.Success);

		pipeline.Dispose();
	}
/*
	[Test]
	public void TestAsyncStateChangeFake()
	{
		bool done = false;
		Pipeline pipeline = new Pipeline(String.Empty);
		Assert.IsNotNull(pipeline, "Could not create pipeline");

		Element src = ElementFactory.Make("fakesrc", null);
		Element sink = ElementFactory.Make("fakesink", null);

		Bin bin = (Bin) pipeline;
		bin.AddMany(src, sink);
		src.Link(sink);

		Bus bus = pipeline.Bus;

		Assert.AreEqual(((Element) pipeline).SetState(State.Playing), StateChangeReturn.Async);

		while(!done) {
			State old, newState, pending;
			Message message = bus.Poll(MessageType.StateChanged, -1);
			if(message != null) {
				message.ParseStateChanged(out old, out newState, out pending);
				//Console.WriteLine("state change from {0} to {1}", old, newState);
				if(message.Src == (Gst.Object) pipeline && newState == State.Playing)
					done = true;
				//message.Dispose();
			}
		}

		Assert.AreEqual(((Element)pipeline).SetState(State.Null), StateChangeReturn.Success);
		//bus.Dispose();
		pipeline.Dispose();
	}
*/
	[Test]
	public void TestGetBus()
	{
		Pipeline pipeline = new Pipeline(String.Empty);
		Assert.IsNotNull(pipeline, "Could not create pipeline");

		Assert.AreEqual(pipeline.Refcount, 1, "Refcount is not 1, it is " + pipeline.Refcount);

		Bus bus = pipeline.Bus;
		Assert.AreEqual(pipeline.Refcount, 1, "Refcount after .Bus");
		Assert.AreEqual(bus.Refcount, 2, "bus.Refcount != 2, it is " + bus.Refcount);

		pipeline.Dispose();

		Assert.AreEqual(bus.Refcount, 1, "bus after unref pipeline");
		bus.Dispose();
	}

	Element pipeline;
	GLib.MainLoop loop;

	bool MessageReceived(Bus bus, Message message) {
		MessageType type = message.Type;

		switch(type) 
		{
			case MessageType.StateChanged:
			{
				State old, newState, pending;
				message.ParseStateChanged(out old, out newState, out pending);
				if(message.Src == (Gst.Object) pipeline && newState == State.Playing) {
					loop.Quit();
				}
				break;
			}
			case MessageType.Error:
				break;
			default: break;
		}
		return true;
	}

	[Test]
	public void TestBus() 
	{
		pipeline = new Pipeline(String.Empty);
		Assert.IsNotNull(pipeline, "Could not create pipeline");
		
		Element src = ElementFactory.Make("fakesrc", null);
		Assert.IsNotNull(src, "Could not create fakesrc");
		Element sink = ElementFactory.Make("fakesink", null);
		Assert.IsNotNull(sink, "Could not create fakesink");
		
		Bin bin = (Bin) pipeline;
		bin.AddMany(src, sink);
		Assert.IsTrue(src.Link(sink), "Could not link between src and sink");
		
		Bus bus = ((Pipeline)pipeline).Bus;
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline's refcount after .Bus");
		Assert.AreEqual(bus.Refcount, 2, "bus");

		uint id = bus.AddWatch(new BusFunc(MessageReceived));
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline after AddWatch");
		Assert.AreEqual(bus.Refcount, 3, "bus after add_watch");
		
		Assert.AreEqual(pipeline.SetState(State.Playing), StateChangeReturn.Async);

		loop = new GLib.MainLoop();
		loop.Run();

		Assert.AreEqual(pipeline.SetState(State.Null), StateChangeReturn.Success);
		State current, pending;
		Assert.AreEqual(pipeline.GetState(out current, out pending, Clock.TimeNone), StateChangeReturn.Success);
		Assert.AreEqual(current, State.Null, "state is not NULL but " + current);
		
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline at start of cleanup");
		Assert.AreEqual(bus.Refcount, 3, "bus at start of cleanup");

		pipeline.Dispose();
		bus.Dispose();
	}
}
