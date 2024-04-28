// TOOD: consider LevelDB and/or `sqlite` and/or PgSQL!

#include "lib_c5t_storage.h"

#include <memory>

#include "bricks/util/singleton.h"
#include "bricks/file/file.h"

// NOTE: Safe, since everything in the file is `JSON<>`-ifified, at least as of now.
static inline std::string kStorageTombstone = "-\n";

class C5T_STORAGE_Instance : public C5T_STORAGE_Interface {
 private:
  std::string const path_;

  // Of type `C5T_FIELD_INTERFACE<T>*` of respective `T`-s.
  std::map<std::string, C5T_STORAGE_FIELD_Interface*> field_inner_impls_;

  // Used to `.clear()` all the containers and force-re-load as changing active storages.
  std::map<C5T_STORAGE_FIELD_Interface const*, bool> initialized_;

 public:
  explicit C5T_STORAGE_Instance(std::string path) : path_(std::move(path)) {
    auto& s = current::Singleton<C5T_Storage_Fields_Singleton>();
    if (s.pimpl) {
      std::cerr << "FATAL: Attempted to use two `C5T_STORAGE` instances." << std::endl;
      ::abort();
    }
    C5T_STORAGE_META_SINGLETON().VisitAllFields(
        [this](C5T_STORAGE_FIELD_Interface* f) { field_inner_impls_[f->Name()] = f; });
    s.pimpl = this;
  }
  ~C5T_STORAGE_Instance() {
    auto& s = current::Singleton<C5T_Storage_Fields_Singleton>();
    if (s.pimpl != this) {
      std::cerr << "FATAL: Confusion with active `C5T_STORAGE` instances." << std::endl;
      ::abort();
    }
    s.pimpl = nullptr;
  }

  size_t FieldsCount() const override { return field_inner_impls_.size(); }

  void ListFields(std::function<void(std::string const&)> cb) override {
    for (auto const& [k, _] : field_inner_impls_) {
      cb(k);
    }
  }

  bool NeedToStartFresh(C5T_STORAGE_FIELD_Interface const& field) override {
    bool& b = initialized_[&field];
    if (!b) {
      b = true;
      return true;
    } else {
      return false;
    }
  }

  void DoSave(std::string const& field, std::string const& key, std::string const& value) override {
#ifdef C5T_DEBUG_STORAGE
    std::cerr << "DoSave(" << path_ << ", " << field << ", " << key << ", " << value << ")\n";
#endif  // C5T_DEBUG_STORAGE
    // TODO: do not create the same dir twice
    current::FileSystem::MkDir(path_ + '/' + field, current::FileSystem::MkDirParameters::Silent);
    try {
      current::FileSystem::WriteStringToFile(value, current::FileSystem::JoinPath(path_, field + '/' + key).c_str());
    } catch (current::Exception const&) {
      // TODO: logging, error handling logic
    }
  }

  Optional<std::string> DoLoad(std::string const& field, std::string const& key) override {
#ifdef C5T_DEBUG_STORAGE
    std::cerr << "DoLoad(" << path_ << ", " << field << ", " << key << ")\n";
#endif  // C5T_DEBUG_STORAGE
    try {
      std::string s =
          current::FileSystem::ReadFileAsString(current::FileSystem::JoinPath(path_, field + '/' + key).c_str());
      if (s != kStorageTombstone) {
        return s;
      } else {
        return nullptr;
      }
    } catch (current::Exception const&) {
      return nullptr;
    }
  }

  void DoDelete(std::string const& field, std::string const& key) override {
#ifdef C5T_DEBUG_STORAGE
    std::cerr << "DoDelete(" << path_ << ", " << field << ", " << key << ")\n";
#endif  // C5T_DEBUG_STORAGE
    current::FileSystem::MkDir(path_ + '/' + field, current::FileSystem::MkDirParameters::Silent);
    try {
      current::FileSystem::WriteStringToFile(kStorageTombstone,
                                             current::FileSystem::JoinPath(path_, field + '/' + key).c_str());
    } catch (current::Exception const&) {
      // TODO: logging, error handling logic
    }
  }

  C5T_STORAGE_FIELD_Interface* UseFieldTypeErased(std::string const& name) override {
    auto const cit = field_inner_impls_.find(name);
    return cit != std::end(field_inner_impls_) ? cit->second : nullptr;
  }
};

std::unique_ptr<C5T_STORAGE_Interface> C5T_STORAGE_CREATE_UNIQUE_INSANCE(std::string const& path) {
#ifdef C5T_DEBUG_STORAGE
  std::cerr << "DoInit(" << path << ")\n";
#endif  // C5T_DEBUG_STORAGE
  current::FileSystem::MkDir(path, current::FileSystem::MkDirParameters::Silent);
  return std::make_unique<C5T_STORAGE_Instance>(path);
}
