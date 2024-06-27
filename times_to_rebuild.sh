#!/bin/bash

TGT=${1:-release}

set -e

if [[ "$OSTYPE" == "darwin"* ]]; then
  echo 'NOTE: On MacOS time is only measured down to seconds. TODO(dkorolev): figure out how to use `date +%N` on MacOS.'
fi

function now_ms {
  if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "$(date +%s)000"
  else
    echo $(( $(date +%s%N) / 1000000 ))
  fi
}

echo 'Running `make '$TGT'` first.'
make $TGT >/dev/null 2>&1
echo

rm -f .tmp-build-times.txt
echo 'Sorted lexicographically:'
for i in $(cd src; ls *.cc | sort); do
  printf "%30s" $i
  T0=$(now_ms)
  touch src/$i
  make $TGT >/dev/null 2>&1
  T1=$(now_ms)
  DT=$((T1-T0))
  printf "\t%d.%.03ds\n" $((DT/1000)) $((DT%1000))
  printf "%4d.%.03ds\t     %s\n" $((DT/1000)) $((DT%1000)) $i >> .tmp-build-times.txt
done
echo

echo 'Now sorted by build time DESC:'
cat .tmp-build-times.txt | sort -gr
echo

echo 'Done.'
