#pragma once

// #define C5T_DEBUG_STORAGE

#include <functional>
#include <string>
#include <unordered_map>

#ifdef C5T_DEBUG_STORAGE
#include <iostream>
#endif  // C5T_DEBUG_STORAGE

// NOTE: No need in JSON stuff in the `.h` file with the interface, although the `.cc` file will need them.
#include "typesystem/optional.h"
#include "typesystem/helpers.h"  // IWYU pragma: keep
#include "bricks/exception.h"

struct StorageKeyNotFoundException final : current::Exception {};
struct StorageNotInitializedException final : current::Exception {};
struct StorageFieldDeclaredAndNotDefinedException final : current::Exception {};
struct StorageInternalErrorException final : current::Exception {};

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
  virtual bool NeedToStartFresh(C5T_STORAGE_FIELD_Interface const&) = 0;

  // Stateless, effectively static, inner methods.
  virtual size_t FieldsCount() const = 0;
  virtual void ListFields(std::function<void(std::string const&)> cb) = 0;

  virtual void DoSave(std::string const& field, std::string const& key, std::string const& value) = 0;
  virtual Optional<std::string> DoLoad(std::string const& field, std::string const& key) = 0;
  virtual void DoDelete(std::string const& field, std::string const& key) = 0;

  // Returns `C5T_STORAGE_FIELD<T>*` of the respective type.
  virtual C5T_STORAGE_FIELD_Interface* UseFieldTypeErased(std::string const&) = 0;
};

class C5T_STORAGE_META_SINGLETON_Impl final {
 private:
  std::vector<C5T_STORAGE_FIELD_Interface*> fields;
  std::set<std::string> field_names;

 public:
  void DeclareField(C5T_STORAGE_FIELD_Interface* f) {
    std::string name = f->Name();
    if (field_names.count(name)) {
      std::cerr << "FATAL: DeclareField('" << name << "') called more than once." << std::endl;
      ::abort();
    }
    field_names.insert(f->Name());
    fields.push_back(f);
  }

  void VisitAllFields(std::function<void(C5T_STORAGE_FIELD_Interface*)> cb) {
    for (C5T_STORAGE_FIELD_Interface* f : fields) {
      cb(f);
    }
  }
};

inline C5T_STORAGE_META_SINGLETON_Impl& C5T_STORAGE_META_SINGLETON() {
  static C5T_STORAGE_META_SINGLETON_Impl impl;
  return impl;
}

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
    if (impl.NeedToStartFresh(self)) {
      contents.clear();
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

inline C5T_STORAGE_Interface& C5T_STORAGE_INSTANCE();

inline void C5T_STORAGE_LIST_FIELDS(std::function<void(std::string const&)> cb) {
  C5T_STORAGE_INSTANCE().ListFields(std::move(cb));
}

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

#define C5T_STORAGE_DECLARE_FIELD(name, T) using C5T_STORAGE_TYPE_##name = T

// NOTE: This should be "called" from some "singleton function", not defined at global scope.
// TODO: There may be a cleaner way, but not now.
#define C5T_STORAGE_DEFINE_FIELD(name, T, meta)                                \
  ([]() {                                                                      \
    class C5T_STORAGE_FIELD_##name final : public C5T_STORAGE_FIELD<T> {       \
     protected:                                                                \
      std::string DoSerializeImpl(void const* p) const {                       \
        return JSON<JSONFormat::Minimalistic>(*reinterpret_cast<T const*>(p)); \
      }                                                                        \
      bool DoDeserializeImpl(std::string const& s, void* p) const {            \
        try {                                                                  \
          ParseJSON<T, JSONFormat::Minimalistic>(s, *reinterpret_cast<T*>(p)); \
          return true;                                                         \
        } catch (current::Exception const&) {                                  \
          return false;                                                        \
        }                                                                      \
      }                                                                        \
                                                                               \
     public:                                                                   \
      C5T_STORAGE_FIELD_##name() : C5T_STORAGE_FIELD(#name) {}                 \
    };                                                                         \
    static C5T_STORAGE_FIELD_##name C5T_STORAGE_FIELD_INSTANCE_##name;         \
  })()

// TODO: rename
struct C5T_Storage_Fields_Singleton final {
  // TODO: atomic?
  C5T_STORAGE_Interface* pimpl = nullptr;
};

// Creates and registers the instance of storage to use.
// Not header-only, requires the `.cc` library to be linked against!
std::unique_ptr<C5T_STORAGE_Interface> C5T_STORAGE_CREATE_UNIQUE_INSANCE(std::string const& path);

// Throws `StorageNotInitializedException` if neither `CREATE`-d nor `INJECT`-ed.
inline C5T_STORAGE_Interface& C5T_STORAGE_INSTANCE() {
  auto& s = current::Singleton<C5T_Storage_Fields_Singleton>();
  if (!s.pimpl) {
    throw StorageNotInitializedException();
  } else {
    return *s.pimpl;
  }
}

// To use an externally-provided storage, mostly for the tests and from within `dlib`-s.
// TODO: Owned/Borrowed or other magic?
inline void C5T_STORAGE_INJECT(C5T_STORAGE_Interface& storage) {
  current::Singleton<C5T_Storage_Fields_Singleton>().pimpl = &storage;
#ifdef C5T_DEBUG_STORAGE
  std::cerr << "InjectStorage(" << storage.FieldsCount() << ")\n";
#endif  // C5T_DEBUG_STORAGE
}

// To access storage fields.
template <class T>
C5T_STORAGE_FIELD_ACCESSOR<T> C5T_STORAGE_USE_FIELD(std::string const& name) {
  auto& storage = C5T_STORAGE_INSTANCE();
  C5T_STORAGE_FIELD_Interface* pimpl = storage.UseFieldTypeErased(name);
  if (!pimpl) {
    throw StorageFieldDeclaredAndNotDefinedException();
  }
  auto pimpl_typed = dynamic_cast<C5T_STORAGE_FIELD<T>*>(pimpl);
  if (!pimpl_typed) {
    throw StorageInternalErrorException();
  }
  return C5T_STORAGE_FIELD_ACCESSOR<T>(*pimpl_typed, storage);
}

#define C5T_STORAGE(name) C5T_STORAGE_USE_FIELD<C5T_STORAGE_TYPE_##name>(#name)
