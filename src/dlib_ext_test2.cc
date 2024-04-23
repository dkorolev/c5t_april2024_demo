#include "test_c5t_dlib.h"

#include "bricks/strings/util.h"

extern "C" void UsesIFooBar(IDLib& iface) {
  // This test actually showcases two `.Use` syntaxes, with `void` and non-`void` return types.
  Optional<int> r = iface.Use<IFoo>([](IFoo& foo) { return foo.FooCalledFromDLib("PASS FOO"); });
  if (Exists(r)) {
    iface.Use<IBar>([v = Value(r)](IBar& bar) { bar.BarCalledFromDLib("PASS BAR, FOO=" + current::ToString(v)); });
  } else {
    iface.Use<IBar>([](IBar& bar) { bar.BarCalledFromDLib("PASS BAR, FOO UNAVAILABLE"); });
  }
}
