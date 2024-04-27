#pragma once

#include "bricks/sync/waitable_atomic.h"
#include "lib_http_server.h"

void RegisterDemoRoutesBasic(current::WaitableAtomic<bool>& time_to_stop_http_server_and_die, HTTPServerContext& ctx);
