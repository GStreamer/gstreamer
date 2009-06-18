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
	public void TestCaps() 
	{
		Gst.Buffer buffer = new Gst.Buffer(4);
		Caps caps = Caps.FromString("audio/x-raw-int");

		Assert.IsNull(buffer.Caps, "buffer.Caps should be null");
		buffer.Caps = caps;
		Assert.IsNotNull(buffer.Caps, "buffer.Caps is null");

		Caps caps2 = Caps.FromString("audio/x-raw-float");
		buffer.Caps = caps2;
		Assert.AreNotEqual(buffer.Caps, caps);
		Assert.AreEqual(buffer.Caps, caps2);

		buffer.Caps = null;
		Assert.IsNull(buffer.Caps, "buffer.Caps should be null");
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

	private void ArrayIsEqual (byte[] a, byte[] b)
	{
		Assert.IsTrue (a.Length == b.Length);
		for (int i = 0; i < a.Length; i++)
			Assert.IsTrue (a[i] == b[i]);
	}

	[Test]
	public void TestBufferData()
	{
		byte[] data = new byte[] {0, 1, 2, 3, 4, 5};

		Gst.Buffer buffer = new Gst.Buffer (data);

		ArrayIsEqual (data, buffer.Data);
		for (uint i = 0; i < buffer.Size; i++)
			Assert.IsTrue (buffer[i] == data[i]);

	}
}
