#include "dlib_ext.h"

#include <atomic>
#include <string>
#include <sstream>

inline std::atomic_int i(0);

std::string foo() { return (std::ostringstream() << "foo, i=" << (++i)).str(); }
