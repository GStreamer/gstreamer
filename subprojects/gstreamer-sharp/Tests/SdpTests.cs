// Copyright (C) 2018 Thibault Saunier <tsaunier@igalia.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301  USA

using NUnit.Framework;
using System;
using Gst;
using Gst.Sdp;

namespace GstSharp.Tests
{
    [TestFixture]
    public class SdpTests : TestBase
    {
        [Test]
        public void TesSdpMessage()
        {
            Gst.Application.Init();
            Gst.Sdp.SDPMessage msg;
            var res = Gst.Sdp.SDPMessage.New(out msg);

            Assert.AreEqual(res, Gst.Sdp.SDPResult.Ok);

            var uri = "udp://nothing.com";
            msg.SetUri(uri);
            Assert.AreEqual(msg.Uri, uri);
        }

    }
}

