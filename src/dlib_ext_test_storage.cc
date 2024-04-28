#include "lib_c5t_dlib.h"
#include "lib_c5t_storage.h"
#include "bricks/strings/join.h"

extern "C" std::string SmokeOK() { return "OK"; }

extern "C" std::string StorageFields(IDLib& iface) {
  std::string res;
  iface.Use<IStorage>([&res](IStorage& storage) {
    C5T_STORAGE_INJECT(storage.Storage());
    std::vector<std::string> v;
    C5T_STORAGE_LIST_FIELDS([&v](std::string const& s) { v.push_back(s); });
    res = current::strings::Join(v, ',');
  });
  return res;
}

extern "C" void AddK(IDLib& iface) {
  iface.Use<IStorage>([](IStorage&) {});
}
