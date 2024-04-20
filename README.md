[![C++](https://img.shields.io/badge/c%2B%2B-17-informational.svg)](https://en.cppreference.com/w/cpp/17) [![./run.sh](https://github.com/dkorolev/c5t_april2024_demo/actions/workflows/run.yml/badge.svg)](https://github.com/dkorolev/c5t_april2024_demo/actions/workflows/run.yml)

# `c5t_april2024_demo`

Here is some holistic-ish demo of several components built recently, most notably:

* The logger.
* The `popen2()` engine via `fork` + `exec`.
* The `dlopen` wrapper to pass interfaces and to re-load the libs dynamically.
* The lifetime manager / graceful shutdown subsystem.
* The pubsub engine.

NOTE(dkorolev): Temporarily, this repo is the source-of-truth for some `lib_c5t_*.{cc,h}` files.

When and how we will add these into `C5T/Current` is an open question for now.
