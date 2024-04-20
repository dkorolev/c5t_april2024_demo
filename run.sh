#!/bin/bash

set -e

make

echo

./.current/demo &

PID=$!

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

get "/stop"

echo
sleep 0.25

wait $PID
echo done

echo
make test
