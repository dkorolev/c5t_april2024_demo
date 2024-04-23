#pragma once

#include "lib_c5t_dlib.h"

struct IMsgReplier : virtual IDLib {
  virtual char const* CurrentMessage() = 0;
  virtual void ReplyToAll(std::string const& s) = 0;
};
