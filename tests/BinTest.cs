//
// BinTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
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
    
    [TestFixtureTearDown]
    public void Deinit()
    {
        Application.Deinit();
    }

    [Test]
    public void TestAddMany()
    {
        Bin bin = new Bin("test-bin");
        Element e1 = ElementFactory.Make("fakesrc", "fakesrc");
        Element e2 = ElementFactory.Make("fakesink", "fakesink");
        bin.AddMany(e1, e2);
        
        Assert.AreEqual(bin.ChildrenCount, 2);
        
        e1.Dispose();
        e2.Dispose();
        bin.Dispose();
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
        
        e1.Dispose();
        bin.Dispose();
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
         Element [] children = bin.Children;
     
         for(int i = 0; i < elements.Length; i++) {
            Assert.AreEqual(elements[i], children[i]);
        }

        bin.Dispose();
    }
}

