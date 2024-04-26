// TOOD: consider LevelDB and/or `sqlite` and/or PgSQL!

#include "lib_c5t_storage.h"

#include <memory>

#include "bricks/util/singleton.h"
#include "bricks/file/file.h"

struct C5T_Storage_Fields_Singleton final {
  C5T_STORAGE_Interface* pimpl = nullptr;
  std::set<std::string> fields;
};

C5T_STORAGE_Interface& C5T_STORAGE_INSTANCE() {
  auto& s = current::Singleton<C5T_Storage_Fields_Singleton>();
  if (!s.pimpl) {
    throw StorageNotInitializedException();
  } else {
    return *s.pimpl;
  }
}

void _C5T_STORAGE_DeclareField(C5T_STORAGE_FIELD_Interface*, std::string const& name) {
  current::Singleton<C5T_Storage_Fields_Singleton>().fields.insert(name);
}

void C5T_STORAGE_LIST_FIELDS(std::function<void(std::string const&)> cb) {
  for (auto const& f : current::Singleton<C5T_Storage_Fields_Singleton>().fields) {
    cb(f);
  }
}

class C5T_STORAGE_LayerImpl final : public C5T_STORAGE_Layer {};

class C5T_STORAGE_Instance : public C5T_STORAGE_Interface {
 private:
  std::string const path_;
  std::map<C5T_STORAGE_FIELD_Interface const*, std::unique_ptr<C5T_STORAGE_LayerImpl>> field_impls_;

 public:
  C5T_STORAGE_Instance(std::string path) : path_(std::move(path)) {
    auto& s = current::Singleton<C5T_Storage_Fields_Singleton>();
    if (s.pimpl) {
      std::cerr << "FATAL: Attempted to use two `C5T_STORAGE` instances." << std::endl;
      ::abort();
    }
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

  C5T_STORAGE_Layer& StorageLayerForField(C5T_STORAGE_FIELD_Interface const& f) override {
    auto& p = field_impls_[&f];
    if (!p) {
      p = std::make_unique<C5T_STORAGE_LayerImpl>();
    }
    return *p;
  }

  void DoSave(std::string const& field, std::string const& key, std::string const& value) override {
    std::cerr << "DoSave(" << path_ << ", " << field << ", " << key << ", " << value << ")\n";
    // TODO: do not create the same dir twice
    current::FileSystem::MkDir(path_ + '/' + field, current::FileSystem::MkDirParameters::Silent);
    try {
      current::FileSystem::WriteStringToFile(value, current::FileSystem::JoinPath(path_, field + '/' + key).c_str());
    } catch (current::Exception const&) {
      // TODO: logging, error handling logic
    }
  }

  Optional<std::string> DoLoad(std::string const& field, std::string const& key) override {
    std::cerr << "DoLoad(" << path_ << ", " << field << ", " << key << ")\n";
    try {
      return current::FileSystem::ReadFileAsString(current::FileSystem::JoinPath(path_, field + '/' + key).c_str());
    } catch (current::Exception const&) {
      return nullptr;
    }
  }
};

std::unique_ptr<C5T_STORAGE_Interface> C5T_STORAGE_CREATE_UNIQUE_INSANCE(std::string const& path) {
  std::cerr << "DoInit(" << path << ")\n";
  current::FileSystem::MkDir(path, current::FileSystem::MkDirParameters::Silent);
  return std::make_unique<C5T_STORAGE_Instance>(path);
}
