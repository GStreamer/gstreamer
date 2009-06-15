//
// Authors
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using Gst;
using System;
using NUnit.Framework;

public class MessageTest {
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
	public void TestParsing()
	{
		Message message = new Message(null);
		Assert.IsNotNull(message);
		Assert.AreEqual(message.Type, MessageType.Eos);
		Assert.IsNull(message.Src);

		message = new Message(null, "error string");
		Assert.IsNotNull(message);
		Assert.AreEqual(message.Type, MessageType.Error);
		Assert.IsNull(message.Src);
	}
}
