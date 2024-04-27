#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include "bricks/net/http/codes.h"

namespace current::http {
struct HTTPServerPOSIX;
}  // namespace current::http

// From `blocks/http/impl/posix_server.h`.
struct FastRequest final {
  std::string const method;
  std::string const& body;
  // const current::net::http::Headers& headers;
  std::vector<std::string> url_path_args;

  FastRequest(std::string method, std::string const& body) : method(method), body(body) {}
};

struct FastResponse final {
  std::string body;
  current::net::HTTPResponseCodeValue code = current::net::HTTPResponseCodeValue::OK;
  std::map<std::string, std::string> headers;
  std::string content_type;
  FastResponse& Body(std::string s) {
    body = std::move(s);
    return *this;
  }
  FastResponse& Code(current::net::HTTPResponseCodeValue c) {
    code = c;
    return *this;
  }
  FastResponse& SetHeader(std::string const& k, std::string v) {
    headers[k] = std::move(v);
    return *this;
  }
  FastResponse& ContentType(std::string s) {
    content_type = std::move(s);
    return *this;
  }
};

struct HTTPServerContext {
  current::http::HTTPServerPOSIX& http;
  void* proutes;  // NOTE(dkorolev): This is super ugly, but will have to do for now.
  HTTPServerContext(current::http::HTTPServerPOSIX& http, void* proutes) : http(http), proutes(proutes) {}

  // From `blocks/url/url.h`.
  using CountMaskUnderlyingType = uint16_t;
  enum { MaxArgsCount = 15 };
  enum class CountMask : CountMaskUnderlyingType {
    None = (1 << 0),
    One = (1 << 1),
    Two = (1 << 2),
    Three = (1 << 3),
    Any = static_cast<CountMaskUnderlyingType>(~0)
  };

  void FastRegister(std::string const& route, CountMask mask, std::function<FastResponse(FastRequest const&)>);
};

void RunHTTPServer(uint16_t port_number, std::function<void(HTTPServerContext&)> code);
