#!/bin/bash

set -e

echo -e '#include <string>\nextern "C" std::string foo() { return "original"; }' >src/dlib_ext_gitignored.cc
make

echo

./.current/demo &

PID=$!

trap "kill $PID" EXIT

sleep 0.25
echo
echo started
echo

function url {
  echo "http://localhost:5555$1"
}

function get {
  URL="$(url $1)"
  echo -n "$URL: "
  curl -s "$URL"
}

function get_silent {
  URL="$(url $1)"
  curl -s "$URL"
}

function assert_get_eq {
  LHS="$(get_silent $1)"
  RHS="$2"
  if [ "$LHS" != "$RHS" ] ; then
    echo "Test failed: via '$1' '$LHS' != '$RHS'."
    exit 1
  fi
}

get "/up"

echo

assert_get_eq "/seq/1" "1"

assert_get_eq "/dlib" "no dlibs loaded"
assert_get_eq "/dlib_reload/foo" "loaded"
assert_get_eq "/dlib_reload/foo" "up to date"
assert_get_eq "/dlib/foo" "has foo(): foo, i=1"
assert_get_eq "/dlib" "foo"
assert_get_eq "/dlib/foo" "has foo(): foo, i=2"
assert_get_eq "/dlib" "foo"

assert_get_eq "/dlib/boo" "no foo()"
assert_get_eq "/dlib" "boo,foo"

assert_get_eq "/dlib/na" "no such dlib"
assert_get_eq "/dlib" "boo,foo"

assert_get_eq "/dlib/gitignored" "has foo(): original"
assert_get_eq "/dlib_reload/gitignored" "up to date"
echo -e '#include <string>\nextern "C" std::string foo() { return "injected"; }' >src/dlib_ext_gitignored.cc
assert_get_eq "/dlib_reload/gitignored" "up to date"
make
assert_get_eq "/dlib_reload/gitignored" "reloaded"
assert_get_eq "/dlib_reload/gitignored" "up to date"
assert_get_eq "/dlib/gitignored" "has foo(): injected"

echo -e '#include <string>\nextern "C" std::string foo() { return "re-injected"; }' >src/dlib_ext_gitignored.cc
make
assert_get_eq "/dlib/gitignored" "has foo(): re-injected"

get "/seq/100" >/dev/null &
PID=$1

sleep 0.25
get "/tasks"

echo

get "/stop"

echo
sleep 0.25

wait $PID
trap - EXIT

echo done

echo
make test
