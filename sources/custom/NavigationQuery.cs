// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

namespace Gst.Video {

	using System;
	using System.Runtime.InteropServices;

	public partial class NavigationAdapter {

		public static bool ParseCommands (Gst.Query query, out NavigationCommand[] cmds) {
			uint len;

			cmds = null;
			if (!QueryParseCommandsLength (query, out len))
				return false;

			cmds = new NavigationCommand[len];

			for (uint i = 0; i < len; i++) {
				NavigationCommand cmd;

				if (!QueryParseCommandsNth (query, i, out cmd))
					return false;
				cmds[i] = cmd;
			}

			return true;
		}
	}
}