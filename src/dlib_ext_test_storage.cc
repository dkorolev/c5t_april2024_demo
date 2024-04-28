#include "lib_c5t_dlib.h"

// Yes, need both the definition and the declaration of the stored fields!
#include "lib_test_storage.h"
#include "lib_test_storage.cc"

// NOTE: Super ugly, need to clean up after the test is passing!
#include "lib_c5t_storage.cc"

#include <string>

extern "C" std::string SmokeOK() { return "OK"; }

extern "C" std::string StorageFields(IDLib& iface) {
  std::string res;
  iface.Use<IStorage>([&res](IStorage& storage) {
    static_cast<void>(storage);
    std::vector<std::string> v;
    C5T_STORAGE_LIST_FIELDS([&v](std::string const& s) { v.push_back(s); });
    res = current::strings::Join(v, ',');
  });
  return res;
}

extern "C" void AddK(IDLib& iface) {
  iface.Use<IStorage>([](IStorage& storage) {
    static_cast<void>(storage);
    // C5T_STORAGE_INJECT(storage.Instance());
    // bar.BarCalledFromDLib("PASS BAR, FOO UNAVAILABLE");
  });
}
