#pragma once

#include <string>

#include "lib_c5t_dlib.h"

struct IFoo : public virtual IDLib {
  virtual int FooCalledFromDLib(std::string const&) = 0;
};

struct IBar : public virtual IDLib {
  virtual void BarCalledFromDLib(std::string const&) = 0;
};
