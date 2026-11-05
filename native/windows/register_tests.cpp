#define wmain KeyrillRegisterMain
#include "register.cpp"
#undef wmain
#include <cstdlib>
#include <wrl/implements.h>

namespace {
void Check(bool passed, const char *message) {
  if (!passed) {
    fprintf(stderr, "Registration test failed: %s.\n", message);
    std::exit(1);
  }
}
class Entries final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IEnumTfLanguageProfiles> {
public:
  Entries(const keyrill::Profile &profile, bool present, HRESULT result)
      : profile_(profile), present_(present), result_(result) {}
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
struct Manager {
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
  HRESULT EnumLanguageProfiles(LANGID language, IEnumTfLanguageProfiles **entries) {
    size_t index = language == keyrill::profiles[0].language ? 0 : 1;
    ++probes[index];
    if (FAILED(enumeration[index]))
      return enumeration[index];
    return Microsoft::WRL::Make<Entries>(keyrill::profiles[index],
                                         present[index] && !(disappear && writes), next)
        .CopyTo(entries);
  }
  HRESULT GetLanguageProfileDescription(REFCLSID, LANGID language, REFGUID, BSTR *value) {
    size_t index = language == keyrill::profiles[0].language ? 0 : 1;
    if (SUCCEEDED(description) && !empty_description)
      *value =
          SysAllocString(wrong_description ? L"unrelated" : keyrill::profiles[index].description);
    return description;
  }
  HRESULT EnableLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL value) {
    ++writes;
    enabled = value;
    return enable_result;
  }
  HRESULT IsEnabledLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL *value) {
    *value = readback_matches ? enabled : !enabled;
    return readback;
  }
};
void Refuses(Manager &manager, HRESULT expected, const char *message) {
  Check(EnableProfiles(&manager, true) == expected, message);
  Check(manager.writes == 0, "failed presence preflight makes zero enablement calls");
  Check(manager.probes[0] == 1 && manager.probes[1] == 1, "both profiles inspected before refusal");
}
} // namespace
int main() {
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
  puts("Registration preflight and acknowledgement schedules passed.");
}
