#pragma once

#include <functional>
#include <string>
#include <unordered_map>

// NOTE: No need in JSON stuff in the `.h` file with the interface, although the `.cc` file will need them.
#include "typesystem/optional.h"
#include "typesystem/helpers.h"  // IWYU pragma: keep
#include "bricks/exception.h"

struct StorageKeyNotFoundException final : current::Exception {};
struct StorageNotInitializedException final : current::Exception {};

// TODO: rethink passing by value to move vs. by reference
struct C5T_STORAGE_Layer {
  bool initialized = false;
};

// TODO: unsure of this is needed at all
class C5T_STORAGE_FIELD_Interface {
 protected:
  C5T_STORAGE_FIELD_Interface() = default;

 public:
  ~C5T_STORAGE_FIELD_Interface() = default;

  virtual std::string const& Name() const = 0;

  // `void*` is `T*`.
  virtual std::string DoSerializeImpl(void const*) const = 0;
  virtual bool DoDeserializeImpl(std::string const&, void*) const = 0;

  // TODO: move this into the storage layer?
  virtual void* GetMapAsVoidPtr(C5T_STORAGE_FIELD_Interface const&) = 0;
};

class C5T_STORAGE_Interface {
 public:
  virtual ~C5T_STORAGE_Interface() = default;
  virtual C5T_STORAGE_Layer& StorageLayerForField(C5T_STORAGE_FIELD_Interface const&) = 0;

  // Stateless, effectively static, inner methods.
  virtual void DoSave(std::string const& field, std::string const& key, std::string const& value) = 0;
  virtual Optional<std::string> DoLoad(std::string const& field, std::string const& key) = 0;
  virtual void DoDelete(std::string const& field, std::string const& key) = 0;
};

class C5T_STORAGE_META_SINGLETON_Impl {
 private:
  std::set<std::string> field_names;

 public:
  virtual void DeclareField(C5T_STORAGE_FIELD_Interface const* f) { field_names.insert(f->Name()); }

  void ListFields(std::function<void(std::string const&)> cb) {
    for (auto const& f : field_names) {
      cb(f);
    }
  }
};

inline C5T_STORAGE_META_SINGLETON_Impl& C5T_STORAGE_META_SINGLETON() {
  static C5T_STORAGE_META_SINGLETON_Impl impl;
  return impl;
}

inline void C5T_STORAGE_LIST_FIELDS(std::function<void(std::string const&)> cb) {
  C5T_STORAGE_META_SINGLETON().ListFields(std::move(cb));
}

// Creates and registers the instance of storage to use.
std::unique_ptr<C5T_STORAGE_Interface> C5T_STORAGE_CREATE_UNIQUE_INSANCE(std::string const& path);

// Throws `StorageNotInitializedException` if neither `CREATE`-d nor `INJECT`-ed.
C5T_STORAGE_Interface& C5T_STORAGE_INSTANCE();

template <class T>
struct C5T_STORAGE_FIELD_TYPES {
  using rhs_t = std::pair<bool, std::unique_ptr<T>>;  // { loaded, value }
  using map_t = std::unordered_map<std::string, rhs_t>;
};

// TODO: maybe make the template type inner, so that `C5T_STORAGE_FIELD` can be passed around?
template <class T>
class C5T_STORAGE_FIELD_ACCESSOR final {
 private:
  C5T_STORAGE_FIELD_Interface& self;
  typename C5T_STORAGE_FIELD_TYPES<T>::map_t& contents;
  C5T_STORAGE_Interface& impl;

  std::unique_ptr<T>& InnerGet(std::string key) const {
    auto& p = contents[key];
    if (!p.first) {
      p.first = true;
      Optional<std::string> s = impl.DoLoad(self.Name(), std::move(key));
      if (Exists(s)) {
        try {
          // TODO: evolve
          if (p.second) {
            if (!self.DoDeserializeImpl(Value(s), p.second.get())) {
              p.second = nullptr;
            }
          } else {
            T instance;
            if (self.DoDeserializeImpl(Value(s), &instance)) {
              p.second = std::make_unique<T>(std::move(instance));
            }
          }
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
    impl.DoSave(self.Name(), key, self.DoSerializeImpl(&value));
    typename C5T_STORAGE_FIELD_TYPES<T>::rhs_t& p = contents[key];
    p.first = true;
    if (p.second) {
      *p.second = value;
    } else {
      p.second = std::make_unique<T>(value);
    }
  }

  void InnerDel(std::string const& key) const {
    if (InnerGet(key)) {
      contents[key].second = nullptr;
      impl.DoDelete(self.Name(), key);
    }
  }

 public:
  C5T_STORAGE_FIELD_ACCESSOR(C5T_STORAGE_FIELD_Interface& self, C5T_STORAGE_Interface& impl)
      : self(self),
        contents(*reinterpret_cast<typename C5T_STORAGE_FIELD_TYPES<T>::map_t*>(self.GetMapAsVoidPtr(self))),
        impl(impl) {
    C5T_STORAGE_Layer& p = impl.StorageLayerForField(self);
    if (!p.initialized) {
      contents.clear();
      p.initialized = true;
    }
  }

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

  void Del(std::string const& key) { InnerDel(key); }
};

template <typename T>
class C5T_STORAGE_FIELD : public C5T_STORAGE_FIELD_Interface {
 private:
  C5T_STORAGE_FIELD() = delete;
  std::string name_;
  mutable typename C5T_STORAGE_FIELD_TYPES<T>::map_t contents_;

 protected:
  C5T_STORAGE_FIELD(char const* name) : name_(name) { C5T_STORAGE_META_SINGLETON().DeclareField(this); }

  std::string const& Name() const override { return name_; }
  void* GetMapAsVoidPtr(C5T_STORAGE_FIELD_Interface const&) override { return &contents_; }

 public:
  // TODO: test `const`-ness.
  C5T_STORAGE_FIELD_ACCESSOR<T> operator()() { return C5T_STORAGE_FIELD_ACCESSOR<T>(*this, C5T_STORAGE_INSTANCE()); }
  C5T_STORAGE_FIELD_ACCESSOR<T> operator()() const {
    return C5T_STORAGE_FIELD_ACCESSOR<T>(*this, C5T_STORAGE_INSTANCE());
  }
};

// TODO: evolve

#define C5T_STORAGE_DECLARE_FIELD(name, type)                             \
  class C5T_STORAGE_FIELD_##name final : public C5T_STORAGE_FIELD<type> { \
   protected:                                                             \
    std::string DoSerializeImpl(void const*) const override;              \
    bool DoDeserializeImpl(std::string const&, void*) const override;     \
                                                                          \
   public:                                                                \
    C5T_STORAGE_FIELD_##name() : C5T_STORAGE_FIELD(#name) {}              \
  };                                                                      \
  extern C5T_STORAGE_FIELD_##name C5T_STORAGE_FIELD_INSTANCE_##name

// NOTE: For `C5T_STORAGE_DEFINE_FIELD` and for `C5T_STORAGE_FIELD`, JSON & serialization need to be `#include`-d.
#define C5T_STORAGE_DEFINE_FIELD(name, type, meta)                                        \
  std::string C5T_STORAGE_FIELD_##name::DoSerializeImpl(void const* p) const {            \
    return JSON<JSONFormat::Minimalistic>(*reinterpret_cast<type const*>(p));             \
  }                                                                                       \
  bool C5T_STORAGE_FIELD_##name::DoDeserializeImpl(std::string const& s, void* p) const { \
    try {                                                                                 \
      ParseJSON<type, JSONFormat::Minimalistic>(s, *reinterpret_cast<type*>(p));          \
      return true;                                                                        \
    } catch (current::Exception const&) {                                                 \
      return false;                                                                       \
    }                                                                                     \
  }                                                                                       \
  C5T_STORAGE_FIELD_##name C5T_STORAGE_FIELD_INSTANCE_##name

// To access storage fields.
#define C5T_STORAGE(name) C5T_STORAGE_FIELD_INSTANCE_##name()

// To use the storage as a singleton.
// #define C5T_STORAGE_SET_BASE_DIR(base_path)  // TODO: implement

// To use an externally-provided storage, mostly for the tests and from within `dlib`-s.
#define C5T_STORAGE_INJECT(x)
