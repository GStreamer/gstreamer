//
// Authors
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using System;
using NUnit.Framework;
using Gst;

public class MessageTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestParsing() {
    Message message = Message.NewEos (null);
    Assert.IsNotNull (message);
    Assert.AreEqual (message.Type, MessageType.Eos);
    Assert.IsNull (message.Src);

    message = Message.NewError (null, CoreError.TooLazy);
    Assert.IsNotNull (message);
    Assert.AreEqual (message.Type, MessageType.Error);
    Assert.IsNull (message.Src);
  }
}
