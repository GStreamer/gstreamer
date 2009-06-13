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
	public void TestErrorNoBus() 
	{
		Element e = ElementFactory.Make("fakesrc", "source");
		e.Dispose();
	}

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

