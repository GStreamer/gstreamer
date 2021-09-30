//   Copyright (C) 2017 Thibault Saunier <thibault.saunier@osg-samsung.com>
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

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	partial class Global {
		public static string TimeFormat(ulong time) {
			return (time / (Gst.Constants.SECOND * 60 * 60)) + ":" +
				(time / (Gst.Constants.SECOND * 60)) % 60 + ":" +
				(time / (Gst.Constants.SECOND)) % 60 + ":" +
				(time % 60);
		}

		public static string TimeFormat(long time) {
			return (time / (Gst.Constants.SECOND * 60 * 60)) + ":" +
				(time / (Gst.Constants.SECOND * 60)) % 60 + ":" +
				(time / (Gst.Constants.SECOND)) % 60 + ":" +
				(time % 60);
		}

	}
}