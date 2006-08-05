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
	public void TestAddRemovePad() 
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		/* create a new floating pad with refcount 1 */
		Pad p = new Pad("source", PadDirection.Src);
		Assert.AreEqual(p.Refcount, 1, "pad");
		
		/* ref it for ourselves */
		Gst.Object.Ref(p.Handle);
		Assert.AreEqual(p.Refcount, 2, "pad");
		/* adding it sinks the pad -> not floating, same refcount */
		e.AddPad(p);
		Assert.AreEqual(p.Refcount, 2, "pad");

		/* removing it reduces the refcount */
		e.RemovePad(p);
		Assert.AreEqual(p.Refcount, 1, "pad");

		/* clean up our own reference */
		p.Dispose();
		e.Dispose();
	}
}

