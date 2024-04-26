#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "typesystem/optional.h"
#include "typesystem/helpers.h"             // IWYU pragma: keep
#include "typesystem/serialization/json.h"  // IWYU pragma: keep
#include "bricks/exception.h"

struct StorageKeyNotFoundException final : current::Exception {};
struct StorageNotInitializedException final : current::Exception {};

// TODO: rethink passing by value to move vs. by reference
struct C5T_STORAGE_Layer {
  bool initialized = false;
};

// TODO: unsure of this is needed at all
struct C5T_STORAGE_FIELD_Interface {
 protected:
  C5T_STORAGE_FIELD_Interface() = default;
};

void _C5T_STORAGE_DeclareField(C5T_STORAGE_FIELD_Interface*, std::string const&);

class C5T_STORAGE_Interface {
 public:
  virtual ~C5T_STORAGE_Interface() = default;
  virtual C5T_STORAGE_Layer& StorageLayerForField(C5T_STORAGE_FIELD_Interface const&) = 0;
  virtual void DoSave(std::string const& field, std::string const& key, std::string const& value) = 0;
  virtual Optional<std::string> DoLoad(std::string const& field, std::string const& key) = 0;
};

// Creates and registers the instance of storage to use.
std::unique_ptr<C5T_STORAGE_Interface> C5T_STORAGE_CREATE_UNIQUE_INSANCE(std::string const& path);

// Throws `StorageNotInitializedException` if neither `CREATE`-d nor `INJECT`-ed.
C5T_STORAGE_Interface& C5T_STORAGE_INSTANCE();

// TODO: maybe make the template type inner, so that `C5T_STORAGE_FIELD` can be passed around?
template <typename T>
class C5T_STORAGE_FIELD : public C5T_STORAGE_FIELD_Interface {
 private:
  C5T_STORAGE_FIELD() = delete;
  using rhs_t = std::pair<bool, std::unique_ptr<T>>;  // { loaded, value }
  std::string name_;
  mutable std::unordered_map<std::string, rhs_t> contents_;

 protected:
  C5T_STORAGE_FIELD(char const* name) : name_(name) { _C5T_STORAGE_DeclareField(this, name_); }

 public:
  // TODO: move from the header file what can be moved to the source file
  class Wrapper final {
   private:
    C5T_STORAGE_FIELD& self;
    C5T_STORAGE_Interface& impl;

    friend class C5T_STORAGE_FIELD;
    Wrapper(C5T_STORAGE_FIELD& self, C5T_STORAGE_Interface& impl) : self(self), impl(impl) {
      C5T_STORAGE_Layer& p = impl.StorageLayerForField(self);
      if (!p.initialized) {
        self.contents_.clear();
        p.initialized = true;
      }
    }

    std::unique_ptr<T>& InnerGet(std::string key) const {
      auto& p = self.contents_[key];
      if (!p.first) {
        p.first = true;
        Optional<std::string> s = impl.DoLoad(self.name_, std::move(key));
        if (Exists(s)) {
          try {
            // TODO: evolve
            p.second = std::make_unique<T>(ParseJSON<T, JSONFormat::Minimalistic>(Value(s)));
          } catch (current::Exception const&) {
            // TODO: log the error, test it
          }
        } else {
          p.second = nullptr;
        }
      }
      return p.second;
    }

    void InnerSet(std::string key, T const& value) const {
      // TODO: better save? incl build time and moving the call to `JSON` our of the header file?
      impl.DoSave(self.name_, key, JSON<JSONFormat::Minimalistic>(value));
      rhs_t& p = self.contents_[key];
      p.first = true;
      if (p.second) {
        *p.second = value;
      } else {
        p.second = std::make_unique<T>(value);
      }
    }

   public:
    bool Has(std::string key) const { return InnerGet(std::move(key)) != nullptr; }

    template <class E = StorageKeyNotFoundException, typename... ARGS>
    T const& GetOrThrow(std::string key, ARGS&&... args) const {
      auto const& p = InnerGet(std::move(key));
      if (p != nullptr) {
        return *p;
      } else {
        throw E(std::forward<ARGS>(args)...);
      }
    }

    Optional<T> Get(std::string key) const {
      auto const& p = InnerGet(std::move(key));
      if (p != nullptr) {
        return *p;
      } else {
        return nullptr;
      }
    }

    template <class TT = T>
    void Set(std::string key, TT&& value) {
      InnerSet(std::move(key), std::forward<TT>(value));
    }

    // TODO: Del
  };
  Wrapper operator()() { return Wrapper(*this, C5T_STORAGE_INSTANCE()); }
  // TODO: test `const`-ness.
  Wrapper operator()() const { return Wrapper(*this, C5T_STORAGE_INSTANCE()); }
};

// TODO: evolve

#define C5T_STORAGE_DECLARE(name, type, meta)                             \
  class C5T_STORAGE_FIELD_##name final : public C5T_STORAGE_FIELD<type> { \
   public:                                                                \
    C5T_STORAGE_FIELD_##name() : C5T_STORAGE_FIELD(#name) {}              \
  } C5T_STORAGE_FIELD_INSTANCE_##name

// To access storage fields.
#define C5T_STORAGE(name) C5T_STORAGE_FIELD_INSTANCE_##name()

// To use the storage as a singleton.
// #define C5T_STORAGE_SET_BASE_DIR(base_path)  // TODO: implement

// To use an externally-provided storage, mostly for the tests and from within `dlib`-s.
#define C5T_STORAGE_INJECT(x)

// TODO: IStorage

// To list the fields.
void C5T_STORAGE_LIST_FIELDS(std::function<void(std::string const&)>);
