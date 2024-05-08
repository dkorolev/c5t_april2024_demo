#!/usr/bin/env python3
#
# This is a rather trivial websocker-to-{stdin/stdout} wrapper:
#
# - It listens on `localhost:5556`.
# - For each websocket connection
#   - It prints "+${idx}" to stdout on it established,
#   - It prints "-${idx}" to stdout on it closed,
#   - It printf "${idx}\t{contents}" to stdout for each message from this websocket, and
#   - For each "{idx}\n{msg}\n" read from stdin it sends the `msg` into Websocket `idx`.

import sys
import asyncio
from websockets.server import serve

wss = {}
idx = 0

async def ws2stdout_impl(s):
  try:
    global wss
    global idx
    idx = idx + 1
    curr = idx
    try:
      print(f"+{curr}")
      sys.stdout.flush()
    except BrokenPipeError:
      sys.exit(1)
    wss[curr] = s
    async for message in s:
      try:
        print(f"{curr}\t{message}")
        sys.stdout.flush()
      except BrokenPipeError:
        sys.exit(1)
    del wss[curr]
    try:
      print(f"-{curr}")
      sys.stdout.flush()
    except BrokenPipeError:
      sys.exit(1)
  except Exception as e:
    try:
      print(e, file=sys.stderr)
    except BrokenPipeError:
      sys.exit(1)
    sys.exit()

async def ws2stdout():
  try:
    async with serve(ws2stdout_impl, "0.0.0.0", 5556):
      await asyncio.Future()
  except Exception as e:
    try:
      print(e, file=sys.stderr)
    except BrokenPipeError:
      sys.exit(1)
    sys.exit()

async def stdin2ws():
  try:
    loop = asyncio.get_event_loop()
    target = None
    while True:
      line = (await loop.run_in_executor(None, sys.stdin.readline)).strip()
      if line:
        if not target:
          target = int(line)
        else:
          if target in wss:
            await wss[target].send(line)
          target = None
  except Exception as e:
    try:
      print(e, file=sys.stderr)
    except BrokenPipeError:
      sys.exit(1)
    sys.exit()

# This is important as otherwise `kill -9`-ing the parent process will result keep this Python code
# up and running, holding the port(s) of the parent process as well, and thus preventing it from starting.
# Since the parent process dying will close the `stderr` stream, writing something into it is
# a sure way to not miss the moment when that parent process is gone for good.
async def timer2stderr():
  while True:
    try:
      sys.stderr.write("\n")
    except BrokenPipeError:
      sys.exit(1)
    await asyncio.sleep(1)

async def main():
  await asyncio.gather(ws2stdout(), stdin2ws(), timer2stderr())

asyncio.run(main())
