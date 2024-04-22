// No `foo()` exposed, but print loading / unloading msgs!
#include <iostream>

extern "C" void OnLoad() { std::cout << "dlib_ext_boo::OnLoad()" << std::endl; }
extern "C" void OnUnload() { std::cout << "dlib_ext_boo::OnUnload()" << std::endl; }
