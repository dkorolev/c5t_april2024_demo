#pragma once

#include <string>
#include "lib_http_server.h"

void RegisterDemoRoutesDLib(std::string const& bin_path, HTTPServerContext& ctx);
