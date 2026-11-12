#include "profiles.h"
#include <cstdio>
#include <cstdlib>
#include <wrl/client.h>
#include <wrl/implements.h>

namespace {
using Registration = HRESULT(WINAPI *)(IUnknown *, uint32_t);
Registration registration = nullptr;
HRESULT EnableProfiles(ITfInputProcessorProfiles *manager, bool enabled) {
  return registration(manager, enabled ? 1u : 2u);
}
HRESULT Preflight(ITfInputProcessorProfiles *manager) { return registration(manager, 0); }
void Check(bool passed, const char *message) {
  if (!passed) {
    fprintf(stderr, "Registration test failed: %s.\n", message);
    std::exit(1);
  }
}
unsigned live_entries = 0;
class Entries final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IEnumTfLanguageProfiles> {
public:
  Entries(const keyrill::Profile &profile, bool present, HRESULT result)
      : profile_(profile), present_(present), result_(result) {
    ++live_entries;
  }
  ~Entries() { --live_entries; }
  HRESULT STDMETHODCALLTYPE Clone(IEnumTfLanguageProfiles **) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Next(ULONG count, TF_LANGUAGEPROFILE *entry, ULONG *fetched) override {
    Check(count == 1 && entry && fetched, "enumeration arguments");
    *fetched = 0;
    if (FAILED(result_))
      return result_;
    if (!present_ || returned_)
      return S_FALSE;
    returned_ = true;
    *entry = {};
    entry->clsid = keyrill::service_id;
    entry->langid = profile_.language;
    entry->guidProfile = profile_.id;
    *fetched = 1;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Reset() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Skip(ULONG) override { return E_NOTIMPL; }

private:
  keyrill::Profile profile_;
  bool present_;
  bool returned_ = false;
  HRESULT result_;
};
struct Manager final : ITfInputProcessorProfiles {
  bool present[2] = {true, true};
  HRESULT enumeration[2] = {S_OK, S_OK};
  HRESULT next = S_OK;
  HRESULT description = S_OK;
  bool empty_description = false;
  bool wrong_description = false;
  HRESULT enable_result = S_OK;
  HRESULT readback = S_OK;
  bool readback_matches = true;
  bool disappear = false;
  BOOL enabled = FALSE;
  unsigned probes[2]{};
  unsigned writes = 0;
  ULONG references = 1;
  bool null_entries = false;
  bool owned_nonexact_entries = false;
  bool throw_enumeration = false;
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
    if (!output)
      return E_POINTER;
    *output = nullptr;
    if (id != IID_IUnknown && id != IID_ITfInputProcessorProfiles)
      return E_NOINTERFACE;
    *output = static_cast<ITfInputProcessorProfiles *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE EnumLanguageProfiles(LANGID language,
                                                 IEnumTfLanguageProfiles **entries) override {
    size_t index = language == keyrill::profiles[0].language ? 0 : 1;
    ++probes[index];
    if (throw_enumeration)
      throw 1;
    if (owned_nonexact_entries) {
      Check(Microsoft::WRL::Make<Entries>(keyrill::profiles[index], true, S_OK).CopyTo(entries) ==
                S_OK,
            "nonexact enumeration transfers one live reference");
      return enumeration[index];
    }
    if (FAILED(enumeration[index]))
      return enumeration[index];
    if (null_entries)
      return enumeration[index];
    if (enumeration[index] != S_OK)
      return enumeration[index];
    return Microsoft::WRL::Make<Entries>(keyrill::profiles[index],
                                         present[index] && !(disappear && writes), next)
        .CopyTo(entries);
  }
  HRESULT STDMETHODCALLTYPE GetLanguageProfileDescription(REFCLSID, LANGID language, REFGUID,
                                                          BSTR *value) override {
    size_t index = language == keyrill::profiles[0].language ? 0 : 1;
    if (SUCCEEDED(description) && !empty_description)
      *value =
          SysAllocString(wrong_description ? L"unrelated" : keyrill::profiles[index].description);
    return description;
  }
  HRESULT STDMETHODCALLTYPE EnableLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL value) override {
    ++writes;
    enabled = value;
    return enable_result;
  }
  HRESULT STDMETHODCALLTYPE IsEnabledLanguageProfile(REFCLSID, LANGID, REFGUID,
                                                     BOOL *value) override {
    *value = readback_matches ? enabled : !enabled;
    return readback;
  }
#define UNUSED_METHOD(name, arguments)                                                             \
  HRESULT STDMETHODCALLTYPE name arguments override { return E_NOTIMPL; }
  UNUSED_METHOD(Register, (REFCLSID));
  UNUSED_METHOD(Unregister, (REFCLSID));
  UNUSED_METHOD(AddLanguageProfile,
                (REFCLSID, LANGID, REFGUID, const WCHAR *, ULONG, const WCHAR *, ULONG, ULONG));
  UNUSED_METHOD(RemoveLanguageProfile, (REFCLSID, LANGID, REFGUID));
  UNUSED_METHOD(EnumInputProcessorInfo, (IEnumGUID **));
  UNUSED_METHOD(GetDefaultLanguageProfile, (LANGID, REFGUID, CLSID *, GUID *));
  UNUSED_METHOD(SetDefaultLanguageProfile, (LANGID, REFCLSID, REFGUID));
  UNUSED_METHOD(ActivateLanguageProfile, (REFCLSID, LANGID, REFGUID));
  UNUSED_METHOD(GetActiveLanguageProfile, (REFCLSID, LANGID *, GUID *));
  UNUSED_METHOD(GetCurrentLanguage, (LANGID *));
  UNUSED_METHOD(ChangeCurrentLanguage, (LANGID));
  UNUSED_METHOD(GetLanguageList, (LANGID **, ULONG *));
  UNUSED_METHOD(EnableLanguageProfileByDefault, (REFCLSID, LANGID, REFGUID, BOOL));
  UNUSED_METHOD(SubstituteKeyboardLayout, (REFCLSID, LANGID, REFGUID, HKL));
#undef UNUSED_METHOD
};
void Refuses(Manager &manager, HRESULT expected, const char *message) {
  Check(EnableProfiles(&manager, true) == expected, message);
  Check(manager.writes == 0, "failed presence preflight makes zero enablement calls");
  Check(manager.probes[0] == 1 && manager.probes[1] == 1, "both profiles inspected before refusal");
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  Check(argc == 2, "pass the actual Rust test DLL path");
  HMODULE module = LoadLibraryW(argv[1]);
  Check(module != nullptr, "Rust registration test DLL loads");
  registration = reinterpret_cast<Registration>(GetProcAddress(module, "KeyrillTestRegistration"));
  Check(registration != nullptr, "registration schedules execute the Rust registrar");
  for (size_t missing = 0; missing < 2; ++missing) {
    Manager manager;
    manager.present[missing] = false;
    Refuses(manager, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "missing profile refuses enablement");
  }
  for (size_t failed = 0; failed < 2; ++failed) {
    Manager manager;
    manager.enumeration[failed] = E_ACCESSDENIED;
    Refuses(manager, E_ACCESSDENIED, "enumeration error refuses enablement");
  }
  Manager next;
  next.next = E_FAIL;
  Refuses(next, E_FAIL, "enumerator Next error refuses enablement");
  Manager description;
  description.description = E_FAIL;
  Refuses(description, E_FAIL, "preference-only enumeration cannot prove registration");
  Manager empty;
  empty.empty_description = true;
  Refuses(empty, E_UNEXPECTED, "missing description refuses enablement");
  Manager wrong;
  wrong.wrong_description = true;
  Refuses(wrong, E_UNEXPECTED, "wrong profile description refuses enablement");
  Manager check;
  Check(Preflight(&check) == S_OK && check.writes == 0, "check is read-only");
  Manager success;
  Check(EnableProfiles(&success, true) == S_OK && success.writes == 2,
        "both present profiles with matching readback succeed");
  Check(EnableProfiles(&success, false) == S_OK && success.writes == 4,
        "disable also verifies readback");
  Manager failed_write;
  failed_write.enable_result = E_ACCESSDENIED;
  Check(EnableProfiles(&failed_write, true) == E_ACCESSDENIED && failed_write.writes == 1,
        "failed enablement stops before the next profile");
  Manager mismatch;
  mismatch.readback_matches = false;
  Check(EnableProfiles(&mismatch, true) == E_FAIL, "S_OK write with wrong readback fails");
  Manager unreadable;
  unreadable.readback = E_ACCESSDENIED;
  Check(EnableProfiles(&unreadable, true) == E_ACCESSDENIED, "readback error fails");
  Manager inconclusive;
  inconclusive.readback = S_FALSE;
  Check(EnableProfiles(&inconclusive, false) == E_UNEXPECTED,
        "S_FALSE with disabled output does not verify disablement");
  Manager unacknowledged;
  unacknowledged.enable_result = S_FALSE;
  Check(EnableProfiles(&unacknowledged, true) == E_UNEXPECTED && unacknowledged.writes == 1,
        "S_FALSE enablement result stops before the next profile");
  Manager disappeared;
  disappeared.disappear = true;
  Check(EnableProfiles(&disappeared, true) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        "profile disappearance after acknowledged calls fails");
  Manager null_entries;
  null_entries.null_entries = true;
  Refuses(null_entries, E_UNEXPECTED, "S_OK null enumerator refuses enablement");
  Manager inconclusive_entries;
  inconclusive_entries.enumeration[0] = S_FALSE;
  Refuses(inconclusive_entries, E_UNEXPECTED, "S_FALSE enumeration refuses enablement");
  Manager inconclusive_description;
  inconclusive_description.description = S_FALSE;
  Refuses(inconclusive_description, E_UNEXPECTED, "S_FALSE description refuses enablement");
  constexpr HRESULT nonexact_results[] = {S_FALSE, E_ACCESSDENIED};
  for (HRESULT result : nonexact_results) {
    Manager owned_nonexact;
    owned_nonexact.owned_nonexact_entries = true;
    owned_nonexact.enumeration[0] = owned_nonexact.enumeration[1] = result;
    Refuses(owned_nonexact, FAILED(result) ? result : E_UNEXPECTED,
            "nonexact owned enumeration refuses enablement");
    Check(live_entries == 0 && owned_nonexact.references == 1,
          "normal nonexact enumeration outputs and manager references drain on refusal");
  }
  Manager throws;
  throws.throw_enumeration = true;
  Refuses(throws, E_UNEXPECTED, "foreign enumeration exception is contained");
  Check(throws.references == 1, "registration call releases its manager reference");
  Check(registration(&success, 3) == E_INVALIDARG, "unknown test operation is rejected");
  Check(registration(nullptr, 0) == E_POINTER, "null registration manager is rejected");
  Check(FreeLibrary(module), "registration test host releases the loaded module");
  puts("Registration preflight and acknowledgement schedules passed.");
}
