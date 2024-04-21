#!/bin/bash

set -e

make

echo

./.current/demo &

PID=$!

trap "echo SIGNAL >/dev/stderr; kill $PID" EXIT

sleep 0.25
echo
echo started
echo

sleep 1.25

function get {
  URL="http://localhost:5555$1"
  echo -n "$URL: "
  curl -s $URL
}

get "/up"

echo

get "/seq"

echo

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

echo
echo done

echo
make test
