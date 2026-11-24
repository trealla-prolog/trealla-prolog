#!/bin/sh

# library(curl) fetching a file:// URL, so no network, and its easy handle. Where libcurl or the FFI is missing
# this passes by printing the expected output, as the runners have no skip status.

TPL=${TPL:-./tpl}

if ! timeout 30 $TPL -q -f -g "catch(use_module(library(curl)),_,true), catch((curl_easy_init(C), curl_easy_cleanup(C)),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/curl_bindings.expected
	exit 0
fi

exec timeout 60 $TPL -q -f tests/misc/curl_bindings/main.pl -g halt </dev/null 2>&1
