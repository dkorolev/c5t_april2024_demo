#include "lib_c5t_dlib.h"
#include "lib_c5t_storage.h"
#include "lib_test_storage.h"  // IWYU pragma: keep
#include "bricks/strings/join.h"

extern "C" int Smoke42() { return 42; }
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

extern "C" void TestSet(IDLib& iface, std::string const& k, std::string const& v) {
  iface.Use<IStorage>([&k, &v](IStorage& storage) {
    C5T_STORAGE_INJECT(storage.Storage());
    C5T_STORAGE(kv1).Set(k, v);
  });
}

extern "C" void TestDel(IDLib& iface, std::string const& k) {
  iface.Use<IStorage>([&k](IStorage& storage) {
    C5T_STORAGE_INJECT(storage.Storage());
    C5T_STORAGE(kv1).Del(k);
  });
}
