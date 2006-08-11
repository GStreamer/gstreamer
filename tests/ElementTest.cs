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

[TestFixture]
public class ElementTest 
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
	public void TestBinAdd() 
	{
		Element src = ElementFactory.Make("fakesrc", null);
		Element sink = ElementFactory.Make("fakesink", null);

		Assert.AreEqual(src.Refcount, 1, "fakesrc");
		Assert.AreEqual(sink.Refcount, 1, "fakesink");

		Element pipeline = new Pipeline(String.Empty);
	
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline");

		Bin bin = (Bin) pipeline;
		Assert.AreEqual(bin.Refcount, 1, "bin");
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline");

		bin.AddMany(src, sink);
		Assert.AreEqual(src.Refcount, 2, "src");
		Assert.AreEqual(sink.Refcount, 2, "sink");
		Assert.AreEqual(bin.Refcount, 1, "bin");
		Assert.AreEqual(pipeline.Refcount, 1, "pipeline");

		src.Link(sink);
		
		src.Dispose();
		sink.Dispose();
		pipeline.Dispose();
	}

	[Test]
	public void TestAddRemovePad() 
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		Pad p = new Pad("source", PadDirection.Src);
		Assert.AreEqual(p.Refcount, 1, "pad");
		
		e.AddPad(p);
		Assert.AreEqual(p.Refcount, 2, "pad");

		e.RemovePad(p);

		p.Dispose();
		e.Dispose();
	}

	[Test]
	public void TestAddPadUnrefElement ()
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		Assert.IsNotNull(e, "Could not create fakesrc");
		Assert.IsTrue(e.GetType() == typeof(Gst.Element));		
		Assert.AreEqual(e.Refcount, 1);

		Pad p = new Pad("source", PadDirection.Src);
		Assert.AreEqual(p.Refcount, 1, "pad");

		Gst.Object.Ref(p.Handle);
		Assert.AreEqual(p.Refcount, 2, "pad");

		e.AddPad(p);
		Assert.AreEqual(p.Refcount, 3, "pad");

		Gst.Object.Unref(p.Handle);
		Assert.AreEqual(p.Refcount, 2, "pad");		

		Assert.AreEqual(e.Refcount, 1);

		e.Dispose();
		Assert.AreEqual(p.Refcount, 1, "pad");

		p.Dispose();
	}

	[Test]
	public void TestErrorNoBus() 
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		e.Dispose();
	}

/*
	[Test] 
	public void TestLink()
	{
		State state, pending;
		
		Element source = ElementFactory.Make("fakesrc", "source");
		Assert.IsNotNull(source);
	
		Element sink = ElementFactory.Make("fakesink", "sink");
		Assert.IsNotNull(sink);
	
		Assert.AreEqual(source.Refcount, 1, source.Name);
		Assert.AreEqual(sink.Refcount, 1, "sink");	
		Assert.IsTrue(source.LinkPads("src", sink, "sink"));
	
		sink.SetState(State.Paused);
		source.SetState(State.Paused);

		Assert.AreEqual(source.Refcount, 1, "src");
		Assert.AreEqual(sink.Refcount, 1, "sink");

		sink.GetState(out state, out pending, Clock.TimeNone);

		sink.SetState(State.Playing);
		source.SetState(State.Playing);

		// Sleep
		System.Threading.Thread.Sleep(500);

		sink.SetState(State.Paused);
		source.SetState(State.Paused);

		sink.GetState(out state, out pending, Clock.TimeNone);

		Assert.AreEqual(sink.Refcount, 1, "sink");
		Assert.AreEqual(source.Refcount, 1, "src");

		source.UnlinkPads("src", sink, "sink");

		Assert.AreEqual(sink.Refcount, 1, "sink");
		Assert.AreEqual(source.Refcount, 1, "src");

		source.Dispose();
		sink.Dispose();
	}
*/

	[Test]
	public void TestLinkNoPads() 
	{
		Element src = new Bin("src");
		Element sink = new Bin("sink");

		Assert.IsFalse(src.Link(sink));

		src.Dispose();
		sink.Dispose();
	}

}

