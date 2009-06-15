//
//  Authors
//    Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using System;
using NUnit.Framework;
using Gst;

[TestFixture]
public class BufferTest
{
	[TestFixtureSetUp]
	public void Init()
	{
		Application.Init();
	}	

	[Test]
	public void TestSubbuffer() 
	{
		Gst.Buffer buffer = new Gst.Buffer(4);
		Gst.Buffer sub = buffer.CreateSub(1, 2);
		Assert.IsNotNull(sub);
		Assert.AreEqual(sub.Size, 2, "subbuffer has wrong size");
	}

	[Test]
	public void TestIsSpanFast()
	{
		Gst.Buffer buffer = new Gst.Buffer(4);

		Gst.Buffer sub1 = buffer.CreateSub(0, 2);
		Assert.IsNotNull(sub1, "CreateSub of buffer returned null");

		Gst.Buffer sub2 = buffer.CreateSub(2, 2);
		Assert.IsNotNull(sub2, "CreateSub of buffer returned null");

		Assert.IsFalse(buffer.IsSpanFast(sub2), "a parent buffer can not be SpanFasted");
		Assert.IsFalse(sub1.IsSpanFast(buffer), "a parent buffer can not be SpanFasted");
		Assert.IsTrue(sub1.IsSpanFast(sub2), "two subbuffers next to each other should be SpanFast");
	}
}
