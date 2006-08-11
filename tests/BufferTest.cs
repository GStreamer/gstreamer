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
public class BufferTest {
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
/*
	[Test]
	public void TestCaps() 
	{
		Gst.Buffer buffer = new Gst.Buffer(4);
		Caps caps = Caps.FromString("audio/x-raw-int");
		Assert.AreEqual(caps.Refcount, 1, "caps");
		Assert.IsNull(buffer.Caps);

		buffer.Caps = caps;
		Assert.AreEqual(caps.Refcount, 2, "caps");

		Assert.AreEqual(caps, buffer.Caps);
		Assert.AreEqual(caps.Refcount, 2, "caps");

		Caps caps2 = Caps.FromString("audio/x-raw-float");
		Assert.AreEqual(caps2.Refcount, 1, "caps2");

		buffer.Caps = caps2;
		Assert.AreEqual(caps.Refcount, 1, "caps");
		Assert.AreEqual(caps2.Refcount, 1, "caps2");

		buffer.Caps = null;
		Assert.AreEqual(caps.Refcount, 1, "caps");
		Assert.AreEqual(caps2.Refcount, 1, "caps2");

		buffer.Caps = caps2;
		Assert.AreEqual(caps2.Refcount, 2, "caps2");
		buffer.Dispose();
		Assert.AreEqual(caps2.Refcount, 1, "caps2");
		caps.Dispose();
		caps2.Dispose();
	}

	[Test]
	public void TestSubbuffer() 
	{
		Gst.Buffer buffer = new Gst.Buffer(4);
		Gst.Buffer sub = buffer.CreateSub(1, 2);
		Assert.IsNotNull(sub);
		Assert.AreEqual(sub.Size, 2, "subbuffer has wrong size");
		//Assert.AreEqual(buffer.Refcount, 2, "parent");
		//Assert.AreEqual(sub.Refcount, 1, "subbuffer");

		//sub.Dispose();
		buffer.Dispose();
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

		buffer.Dispose();
	}
*/
}

