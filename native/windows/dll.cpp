#include "service.h"
#include <new>

namespace {
class Factory final : public IClassFactory {
public:
  Factory() { InterlockedIncrement(&keyrill::object_count); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_IClassFactory)
      return E_NOINTERFACE;
    *out = static_cast<IClassFactory *>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG n = --references_;
    if (!n)
      delete this;
    return n;
  }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (outer)
      return CLASS_E_NOAGGREGATION;
    auto *service = new (std::nothrow) keyrill::Service;
    if (!service)
      return E_OUTOFMEMORY;
    HRESULT result = service->QueryInterface(id, out);
    service->Release();
    return result;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
    if (lock)
      InterlockedIncrement(&keyrill::object_count);
    else
      InterlockedDecrement(&keyrill::object_count);
    return S_OK;
  }

private:
  ~Factory() { InterlockedDecrement(&keyrill::object_count); }
  ULONG references_ = 1;
};
} // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID id, void **out) {
  if (!out)
    return E_POINTER;
  *out = nullptr;
  if (clsid != keyrill::service_id)
    return CLASS_E_CLASSNOTAVAILABLE;
  auto *factory = new (std::nothrow) Factory;
  if (!factory)
    return E_OUTOFMEMORY;
  HRESULT result = factory->QueryInterface(id, out);
  factory->Release();
  return result;
}
STDAPI DllCanUnloadNow() {
  return InterlockedCompareExchange(&keyrill::object_count, 0, 0) == 0 &&
                 InterlockedCompareExchange(&keyrill::vocabulary::worker_count, 0, 0) == 0
             ? S_OK
             : S_FALSE;
}
