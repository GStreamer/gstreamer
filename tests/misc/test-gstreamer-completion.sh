#!/bin/bash

. $(dirname "$0")/../../tools/gstreamer-completion
ret=0


test_gst_inspect_completion() {
    local expected
    COMP_WORDS=(gst-inspect)
    while [[ "$1" != -- ]]; do COMP_WORDS+=("$1"); shift; done; shift
    COMP_CWORD=$(( ${#COMP_WORDS[*]} - 1 ))
    COMP_LINE="${COMP_WORDS[*]}"
    COMP_POINT=${#COMP_LINE}
    expected=(); while [[ -n "$1" ]]; do expected+=("$1"); shift; done

    printf "test_gst_inspect_completion: '${COMP_WORDS[*]}'... "
    _gst_inspect

    _assert_expected && echo OK
}

_assert_expected() {
    for x in "${expected[@]}"; do
        grep -w -q -- "$x" <(echo "${COMPREPLY[*]}") &>/dev/null || {
            ret=1
            echo FAIL
            echo "Expected: '$x'. Got:"
            for r in "${COMPREPLY[@]}"; do echo $r; done | head
            echo ""
            return 1
        }
    done
    return 0
}

# test_gst_inspect_completion <command line to complete> -- <expected completions>
test_gst_inspect_completion '' -- --version --gst-debug-level coreelements fakesrc
test_gst_inspect_completion --ver -- --version
test_gst_inspect_completion --gst-debug-le -- --gst-debug-level
test_gst_inspect_completion --gst-debug-level '' -- 0 1 2 3 4 5
test_gst_inspect_completion --gst-debug-level = -- 0 1 2 3 4 5
test_gst_inspect_completion --gst-debug-level= -- 0 1 2 3 4 5
test_gst_inspect_completion --gst-debug-level=4 -- 4
test_gst_inspect_completion coreel -- coreelements
test_gst_inspect_completion fake -- fakesrc fakesink
test_gst_inspect_completion --version --gst-debug-level = 2 fake -- fakesrc fakesink
test_gst_inspect_completion --gst-debug-level=2 fake -- fakesrc fakesink


test_gst_launch_completion() {
    local expected
    COMP_WORDS=(gst-launch)
    while [[ "$1" != -- ]]; do COMP_WORDS+=("$1"); shift; done; shift
    COMP_CWORD=$(( ${#COMP_WORDS[*]} - 1 ))
    COMP_LINE="${COMP_WORDS[*]}"
    COMP_POINT=${#COMP_LINE}
    expected=(); while [[ -n "$1" ]]; do expected+=("$1"); shift; done

    printf "test_gst_launch_completion: '${COMP_WORDS[*]}'... "
    _gst_launch

    _assert_expected &&
    echo OK
}

# test_gst_launch_completion <command line to complete> -- <expected completions>
test_gst_launch_completion '' -- --eos-on-shutdown --gst-debug-level fakesrc fakesink
test_gst_launch_completion --mes -- --messages
test_gst_launch_completion --gst-debug-le -- --gst-debug-level
test_gst_launch_completion --gst-debug-level '' -- 0 1 2 3 4 5
test_gst_launch_completion --gst-debug-level = -- 0 1 2 3 4 5
test_gst_launch_completion --gst-debug-level= -- 0 1 2 3 4 5
test_gst_launch_completion --gst-debug-level=4 -- 4
test_gst_launch_completion fak -- fakesrc fakesink
test_gst_launch_completion --messages fak -- fakesrc fakesink
test_gst_launch_completion --messages --eos-on-shutdown fak -- fakesrc
test_gst_launch_completion --gst-debug-level = 4 fak -- fakesrc
test_gst_launch_completion --gst-debug-level=4 fak -- fakesrc
test_gst_launch_completion fakesrc '' -- name= is-live= format= !
test_gst_launch_completion fakesrc is-live -- is-live=
test_gst_launch_completion fakesrc is-live = -- true false
test_gst_launch_completion fakesrc format = -- bytes time buffers percent
test_gst_launch_completion fakesrc format= -- bytes time buffers percent
test_gst_launch_completion fakesrc format=by -- bytes
test_gst_launch_completion fakesrc format= '' -- bytes time buffers percent
test_gst_launch_completion fakesrc format= by -- bytes
test_gst_launch_completion fakesrc is-live = true '' -- name= format= !
test_gst_launch_completion fakesrc is-live = true for -- format=
test_gst_launch_completion fakesrc is-live=true '' -- name= format= !
test_gst_launch_completion fakesrc is-live=true for -- format=
test_gst_launch_completion fakesrc is-live = true format = -- bytes time
test_gst_launch_completion fakesrc is-live=true format= -- bytes time


test_gst_launch_parse() {
    local cur cword words curtype option element property
    words=(gst-launch)
    while [[ "$1" != -- ]]; do words+=("$1"); shift; done; shift
    cword=$(( ${#words[*]} - 1 ))
    cur="${words[cword]}"
    local xcurtype="$1" xoption="$2" xelement="$3" xproperty="$4"

    printf "test_gst_launch_parse: '${words[*]}'... "
    _gst_launch_parse

    _assert curtype "$curtype" "$xcurtype" &&
    _assert option "$option" "$xoption" &&
    _assert element "$element" "$xelement" &&
    _assert property "$property" "$xproperty" &&
    echo OK
}

_assert() {
    local name="$1" got="$2" expected="$3"
    [[ -z "$expected" || "$got" == "$expected" ]] || {
        ret=1
        echo "FAIL"
        echo "Expected $name: '$expected'. Got: '$got'."
        echo ""
        false
    }
}

test_gst_launch_parse '' -- option-or-element '' '' ''
test_gst_launch_parse --mes -- option '' '' ''
test_gst_launch_parse --messages -- option '' '' ''
test_gst_launch_parse --gst-debug-level '' -- optionval --gst-debug-level '' ''
test_gst_launch_parse --gst-debug-level = -- optionval --gst-debug-level '' ''
test_gst_launch_parse --gst-debug-level= -- optionval --gst-debug-level '' ''
test_gst_launch_parse --gst-debug-level=5 -- optionval --gst-debug-level '' ''
test_gst_launch_parse fak -- element '' '' ''
test_gst_launch_parse --messages fak -- element '' '' ''
test_gst_launch_parse --gst-debug-level = 5 fak -- element '' '' ''
test_gst_launch_parse fakesrc '' -- property '' fakesrc ''
test_gst_launch_parse fakesrc is-l -- property '' fakesrc ''
test_gst_launch_parse fakesrc is-live = -- propertyval '' fakesrc is-live
test_gst_launch_parse fakesrc is-live= -- propertyval '' fakesrc is-live
test_gst_launch_parse fakesrc is-live=b -- propertyval '' fakesrc is-live
test_gst_launch_parse fakesrc is-live = true form -- property '' 'fakesrc' ''
test_gst_launch_parse fakesrc is-live = true ! -- ! '' '' ''
test_gst_launch_parse fakesrc is-live = true ! fakesi -- element '' '' ''
test_gst_launch_parse fakesrc is-live = true ! fakesink '' -- property '' fakesink ''


exit $ret
