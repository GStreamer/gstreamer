//
// BinTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//   Khaled Mohammed (Khaled.Mohammed@gmail.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;
using Gst;

[TestFixture]
public class BinTest
{
	[TestFixtureSetUp]
	public void Init()
	{
		Application.Init();
	}

	[Test]
	public void TestAdd()
	{
		Bin bin = new Bin("test-bin");
		Element e1 = ElementFactory.Make("fakesrc", "fakesrc");
		Element e2 = ElementFactory.Make("fakesink", "fakesink");

		Assert.IsNotNull(bin, "Could not create bin");
		Assert.IsNotNull(e1, "Could not create fakesrc");
		Assert.IsNotNull(e2, "Could not create fakesink");

		bin.Add(e1, e2);

		Assert.AreEqual(bin.ChildrenCount, 2);
	}

	[Test]
	public void TestGetByName()
	{
		Bin bin = new Bin("test-bin");
		Element e1 = ElementFactory.Make("fakesrc", "element-name");
		bin.Add(e1);

		e1 = bin.GetByName("element-name");

		Assert.IsNotNull(e1);
		Assert.AreEqual(e1.Name, "element-name");
	}

	[Test]
	public void TestChildren()
	{
		Bin bin = new Bin("test-bin");

		Element [] elements = new Element [] {
			ElementFactory.Make("fakesrc", "fakesrc"),
				ElementFactory.Make("audioconvert", "audioconvert"),
				ElementFactory.Make("wavenc", "wavenc"),
				ElementFactory.Make("fakesink", "fakesink")
		};

		foreach(Element element in elements) {
			bin.Add(element);
		}

		Assert.AreEqual(elements.Length, bin.ChildrenCount);

		for(uint i = 0; i < elements.Length; i++) {
			Assert.AreEqual(elements[elements.Length - i - 1], bin.GetChildByIndex(i));
		}
	}

	[Test]
	public void TestInterface()
	{
		Bin bin = new Bin(String.Empty);
		Assert.IsNotNull(bin, "Could not create bin");

		Element filesrc = ElementFactory.Make("filesrc", null);
		Assert.IsNotNull(filesrc, "Could not create filesrc");

		bin.Add(filesrc);
	}
}
