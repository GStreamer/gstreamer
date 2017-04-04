# GStreamer
# Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
#
# bash/zsh completion support for gst-launch
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

_GST_HELPERDIR="${BASH_SOURCE[0]%/*}/../helpers"

if [[ ! -d "$_GST_HELPERDIR" ]]; then
	_GST_HELPERDIR="$(pkg-config --variable=bashhelpersdir gstreamer-1.0)"
else
	_GST_HELPERDIR=`cd "$_GST_HELPERDIR"; pwd`
fi

# Common definitions
. "$_GST_HELPERDIR"/gst

_gst_launch_all_arguments ()
{
	_gst_all_arguments gst-launch-1.0
}

_gst_complete_compatible_elements ()
{
	COMPREPLY=( $(compgen -W "$($_GST_HELPER --compatible-with $previous_element)" -- $cur) )
}

_gst_complete_all_elements ()
{
	COMPREPLY=( $(compgen -W "$($_GST_HELPER -l)" -- $cur) )
}

_gst_complete_element_properties ()
{
	COMPREPLY=( $(compgen -W "$($_GST_HELPER --element-properties $previous_element)" -- $cur) )
}

_gstlaunch___exclude_ () { _gst_mandatory_argument gst-launch-1.0; }

_gst_launch_main ()
{
	local i=1 command function_exists previous_element have_previous_element=0 completion_func

	while [[ $i -ne $COMP_CWORD ]];
		do
			local var
			var="${COMP_WORDS[i]}"
			if [[ "$var" == "-"* ]]
			then
				command="$var"
			fi
		i=$(($i+1))
		done

	i=1
	while [[ $i -ne $COMP_CWORD ]];
		do
			local var
			var="${COMP_WORDS[i]}"

			if [[ "$var" == "-"* ]]
			then
				i=$(($i+1))
				continue
			fi

			$(gst-inspect-1.0 --exists $var)
			if [ $? -eq 0 ]
			then
				previous_element="$var"
				have_previous_element=1
			fi
		i=$(($i+1))
		done

	if [[ "$command" == "--gst"* ]]; then
		completion_func="_${command//-/_}"
	else
		completion_func="_gstlaunch_${command//-/_}"
	fi

	# Seems like bash doesn't like "exclude" in function names
	if [[ "$completion_func" == "_gstlaunch___exclude" ]]
	then
		completion_func="_gstlaunch___exclude_"
	fi

	declare -f $completion_func >/dev/null 2>&1

	function_exists=$?

	if [[ "$cur" == "-"* ]]; then
		_gst_launch_all_arguments
	elif [ $function_exists -eq 0 ]
	then
		$completion_func
	elif [ $have_previous_element -ne 0 ] && [[ "$prev" == "!" ]]
	then
		_gst_complete_compatible_elements
	elif [ $have_previous_element -ne 0 ]
	then
		_gst_complete_element_properties
	else
		_gst_complete_all_elements
	fi
}

_gst_launch_func_wrap ()
{
	local cur prev
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD-1]}"
	$1
}

# Setup completion for certain functions defined above by setting common
# variables and workarounds.
# This is NOT a public function; use at your own risk.
_gst_launch_complete ()
{
	local wrapper="__launch_wrap${2}"
	eval "$wrapper () { _gst_launch_func_wrap $2 ; }"
	complete -o bashdefault -o default -o nospace -F $wrapper $1 2>/dev/null \
		|| complete -o default -o nospace -F $wrapper $1
}

_gst_launch_complete gst-launch-1.0 _gst_launch_main
