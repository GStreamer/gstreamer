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

	[Test]
	public void TestLinkNoPads() 
	{
		Element src = new Bin("src");
		Element sink = new Bin("sink");

		Assert.IsFalse(src.Link(sink));
		Assert.IsFalse(Element.Link(src, sink));
	}

	[Test]
	public void TestAddRemovePad()
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		Pad pad = new Pad("source", PadDirection.Src);

		e.AddPad(pad);
		Assert.AreEqual(pad, e.GetStaticPad("source"));

		e.RemovePad(pad);
		Assert.IsNull(e.GetStaticPad("source"));
	}

	[Test]
	public void TestLink()
	{
		State state, pending;

		Element source = ElementFactory.Make("fakesrc", "source");
		Assert.IsNotNull(source);
		Element sink = ElementFactory.Make("fakesink", "sink");
		Assert.IsNotNull(sink);

		Assert.IsTrue(source.LinkPads("src", sink, "sink"));

		sink.SetState(State.Paused);
		source.SetState(State.Paused);
		sink.GetState(out state, out pending, Clock.TimeNone);
		Assert.AreEqual(state, State.Paused);

		sink.SetState(State.Playing);
		source.SetState(State.Playing);
		source.GetState(out state, out pending, Clock.TimeNone);
		Assert.AreEqual(state, State.Playing);

		sink.SetState(State.Null);
		source.SetState(State.Null);
		sink.GetState(out state, out pending, Clock.TimeNone);
		Assert.AreEqual(state, State.Null);

		Assert.AreEqual(source.GetStaticPad("src").Peer, sink.GetStaticPad("sink"));
		source.Unlink(sink);
		Assert.IsFalse(source.GetStaticPad("src").IsLinked);
	}
}
