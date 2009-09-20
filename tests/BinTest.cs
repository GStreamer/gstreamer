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
using Gst.CorePlugins;

[TestFixture]
public class BinTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestAdd() {
    Bin bin = new Bin ("test-bin");
    Element e1 = new FakeSrc ("fakesrc");
    Element e2 = new FakeSink ("fakesink");

    Assert.IsNotNull (bin, "Could not create bin");
    Assert.IsNotNull (e1, "Could not create fakesrc");
    Assert.IsNotNull (e2, "Could not create fakesink");

    bin.Add (e1, e2);

    Assert.AreEqual (bin.ChildrenCount, 2);
  }

  [Test]
  public void TestAddRemove() {
    Bin bin = ElementFactory.Make ("bin") as Bin;
    Assert.IsNotNull (bin, "Could not create bin");

    Element e1 = new FakeSrc ("fakesrc");
    Element e2 = new Identity ("identity");
    Element e3 = new FakeSink ("fakesink");

    Assert.IsNotNull (e1, "Could not create fakesrc");
    Assert.IsNotNull (e2, "Could not create identity");
    Assert.IsNotNull (e3, "Could not create fakesink");

    bin.Add (e1, e2, e3);
    Element.Link (e1, e2, e3);

    Assert.AreEqual (bin.ChildrenCount, 3);
    bin.Remove (e2, e3);
    Assert.AreEqual (bin.ChildrenCount, 1);
    bin.Add (e2);
    Assert.AreEqual (bin.ChildrenCount, 2);
    bin.Remove (e1, e2);
    Assert.AreEqual (bin.ChildrenCount, 0);
  }

  [Test]
  public void TestGetByName() {
    Bin bin = new Bin ("test-bin");
    Element e1 = ElementFactory.Make ("fakesrc", "element-name");
    bin.Add (e1);

    e1 = bin.GetByName ("element-name");

    Assert.IsNotNull (e1);
    Assert.AreEqual (e1.Name, "element-name");
  }

  [Test]
  public void TestGetChildByIndex() {
    Bin bin = new Bin ("test-bin");

    Element [] elements = new Element [] {
      ElementFactory.Make ("fakesrc", "fakesrc"),
      ElementFactory.Make ("audioconvert", "audioconvert"),
      ElementFactory.Make ("wavenc", "wavenc"),
      ElementFactory.Make ("fakesink", "fakesink")
    };

    foreach (Element element in elements) {
      bin.Add (element);
    }

    Assert.AreEqual (elements.Length, bin.ChildrenCount);

    for (uint i = 0; i < elements.Length; i++) {
      Assert.AreEqual (elements[elements.Length - i - 1], bin.GetChildByIndex (i));
    }
  }

  [Test]
  public void TestElements() {
    Bin bin = new Bin ("test-bin");

    Element [] elements = new Element [] {
      new CapsFilter(),
      new MultiQueue(),
      new Queue(),
      new Tee(),
      new TypeFindElement()
    };

    bin.Add (elements);
    CollectionAssert.AreEquivalent (elements, bin.Elements);
    CollectionAssert.AreEquivalent (elements, bin.ElementsRecurse);
    CollectionAssert.AreEquivalent (elements, bin.ElementsSorted);
  }

  public class MyBin : Gst.Bin {
    public MyBin () : base () {
      Element filesrc = ElementFactory.Make ("filesrc");
      Add (filesrc);
      CollectionAssert.IsEmpty (Pads);

      GhostPad pad1 = new GhostPad ("ghost-sink", PadDirection.Sink);
      GhostPad pad2 = new GhostPad ("ghost-src", new PadTemplate ("src-template", PadDirection.Src, PadPresence.Request, Caps.NewAny()));

      Assert.IsFalse (pad1.SetTarget (filesrc.GetStaticPad ("src")));
      Assert.IsTrue (pad2.SetTarget (filesrc.GetStaticPad ("src")));

      AddPad (pad1);
      AddPad (pad2);

      CollectionAssert.Contains (Pads, pad1);
      CollectionAssert.Contains (Pads, pad2);
      CollectionAssert.Contains (SinkPads, pad1);
      CollectionAssert.Contains (SrcPads, pad2);
    }
  }

  [Test]
  public void TestGhostPad() {
    new MyBin ();
  }
}
