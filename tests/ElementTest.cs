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
	public void TestGoodConstructor()
	{
		Element sink = ElementFactory.Make("fakesink", "fake-sink");

		Assert.IsNotNull(sink, "fakesink plugin is not installed?");
		Assert.IsFalse(sink.Handle == IntPtr.Zero, "sink Element has null handle");
		//Assert.IsInstanceOfType(typeof(Element), sink, "sink is not an Element?");
		Assert.AreEqual(sink.Name, "fake-sink");

		sink.Dispose();
	}

	[Test]
	public void TestAddingAndRemovingPads()
	{
		Element src = ElementFactory.Make("fakesrc", "fake-src");

		Assert.IsNotNull(src, "fakesrc plugin is not installed?");
		
		Pad [] pads = new Pad[2];

		pads[0] = new Pad("src1", PadDirection.Src);
		pads[1] = new Pad("src2", PadDirection.Sink);

		foreach(Pad P in pads) {
			src.AddPad(P);
		}


		foreach(Pad P in pads) {
			//Assert.IsTrue(src.Pads.IndexOf(P) >= 0);
		}

		foreach(Pad P in pads) {
			Assert.IsTrue(src.RemovePad(P));
		}

	}
}

