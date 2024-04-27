#pragma once

#include <cstdint>
#include <functional>

namespace current::http {
struct HTTPServerPOSIX;
}  // namespace current::http

struct HTTPServerContext {
  current::http::HTTPServerPOSIX& http;
  void* proutes;  // NOTE(dkorolev): This is super ugly, but will have to do for now.
  HTTPServerContext(current::http::HTTPServerPOSIX& http, void* proutes) : http(http), proutes(proutes) {}
};

void RunHTTPServer(uint16_t port_number, std::function<void(HTTPServerContext&)> code);
