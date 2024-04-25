#pragma once

#include <string>
#include <unordered_map>

#include "typesystem/optional.h"
#include "typesystem/helpers.h"  // IWYU pragma: keep
#include "bricks/exception.h"

struct StorageKeyNotFoundException : current::Exception {};

template <typename T>
class C5T_STORAGE_FIELD {
 private:
  std::unordered_map<std::string, T> contents_;

 protected:
  C5T_STORAGE_FIELD() {
    // TODO: un-persist
  }

 public:
  template <class TT = T>
  void Set(std::string const& key, TT&& value) {
    contents_[key] = std::forward<TT>(value);
    // TODO: persist
  }
  bool Has(std::string const& key) const { return contents_.count(key); }
  template <class E = StorageKeyNotFoundException, typename... ARGS>
  T const& GetOrThrow(std::string const& key, ARGS&&... args) const {
    auto const cit = contents_.find(key);
    if (cit != std::end(contents_)) {
      return cit->second;
    } else {
      throw E(std::forward<ARGS>(args)...);
    }
  }
  Optional<T> Get(std::string const& key) const {
    auto const cit = contents_.find(key);
    if (cit != std::end(contents_)) {
      return cit->second;
    } else {
      return nullptr;
    }
  }
};

// TODO: evolve

#define C5T_STORAGE_SET_BASE_DIR(base_path)  // TODO: implement

#define C5T_STORAGE_DECLARE(name, type, meta)                             \
  class C5T_STORAGE_FIELD_##name final : public C5T_STORAGE_FIELD<type> { \
  } C5T_STORAGE_FIELD_INSTANCE_##name

#define C5T_STORAGE(name) C5T_STORAGE_FIELD_INSTANCE_##name
