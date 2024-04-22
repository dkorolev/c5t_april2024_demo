#include "dlib_ext.h"

#include <atomic>
#include <iostream>
#include <string>
#include <sstream>

inline std::atomic_int i(0);

std::string foo() { return (std::ostringstream() << "foo, i=" << (++i)).str(); }

void OnLoad() { std::cout << "dlib_ext_foo::OnLoad()" << std::endl; }
void OnUnload() { std::cout << "dlib_ext_foo::OnUnload()" << std::endl; }
