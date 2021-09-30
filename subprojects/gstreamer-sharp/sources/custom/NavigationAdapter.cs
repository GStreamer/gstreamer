//
// Authors:
//   Sebastian Dröge <slomo@circular-chaos.org>
//
// Copyright (C) 2009 Sebastian Dröge
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

namespace Gst.Video {

	using System;
	using System.Runtime.InteropServices;

	public partial class NavigationAdapter {

		public static bool ParseCommands(Gst.Query query, out NavigationCommand[] cmds) {
			uint len;

			cmds = null;
			if (!QueryParseCommandsLength(query, out len))
				return false;

			cmds = new NavigationCommand[len];

			for (uint i = 0; i < len; i++) {
				NavigationCommand cmd;

				if (!QueryParseCommandsNth(query, i, out cmd))
					return false;
				cmds[i] = cmd;
			}

			return true;
		}
	}
}