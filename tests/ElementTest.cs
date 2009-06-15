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
	public void TestErrorNoBus() 
	{
		Element e = ElementFactory.Make("fakesrc", "source");
	}

	[Test]
	public void TestLinkNoPads() 
	{
		Element src = new Bin("src");
		Element sink = new Bin("sink");

		Assert.IsFalse(src.Link(sink));
	}
}
