#pragma once
#include "profiles.h"
#include "vocabulary.h"
#include <cwchar>
#include <string_view>
#include <unknwn.h>
#include <wrl/client.h>

namespace keyrill::runtime {
inline constexpr uint32_t version = 1;
inline constexpr GUID reader_iid = {
    0xcda9ed25, 0x1dbc, 0x4fb6, {0xb2, 0xa5, 0xbc, 0x13, 0x29, 0x45, 0xe0, 0x82}};
struct Snapshot {
  uint32_t activation = 0;
  uint32_t requested = KR_POLICY_OFF;
  uint32_t effective = KR_POLICY_OFF;
  uint32_t model_ready = 0;
  uint64_t revision = 0;
  vocabulary::Error error = vocabulary::Error::None;
  vocabulary::Error configuration_error = vocabulary::Error::None;
  std::array<vocabulary::Status, 2> sources{};
  std::array<vocabulary::Error, 2> source_errors{};
};
static_assert(sizeof(Snapshot) == 48);
struct Reader : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Copy(uint32_t size, uint32_t requested_version, Snapshot *) = 0;
};
inline constexpr size_t status_capacity = 512;
inline constexpr int status_id = 111;
inline constexpr wchar_t format[] =
    L"Keyrill runtime v1; activation=%u; requested=%u; effective=%u; model_ready=%u; "
    L"revision=%llu; error=%u; configuration_error=%u; en_state=%u; en_error=%u; "
    L"ru_state=%u; ru_error=%u";
inline bool Format(const Snapshot &value, wchar_t (&output)[status_capacity]) noexcept {
  return swprintf_s(output, format, value.activation, value.requested, value.effective,
                    value.model_ready, value.revision, static_cast<uint32_t>(value.error),
                    static_cast<uint32_t>(value.configuration_error),
                    static_cast<uint32_t>(value.sources[0]),
                    static_cast<uint32_t>(value.source_errors[0]),
                    static_cast<uint32_t>(value.sources[1]),
                    static_cast<uint32_t>(value.source_errors[1])) > 0;
}
inline bool Parse(std::wstring_view text, Snapshot &value) try {
  if (text.empty() || text.size() >= status_capacity)
    return false;
  Snapshot candidate;
  uint32_t error = 0, configuration_error = 0, en = 0, en_error = 0, ru = 0, ru_error = 0;
  const std::wstring input(text);
  if (swscanf_s(input.c_str(), format, &candidate.activation, &candidate.requested,
                &candidate.effective, &candidate.model_ready, &candidate.revision, &error,
                &configuration_error, &en, &en_error, &ru, &ru_error) != 11 ||
      candidate.requested > KR_POLICY_MODEL || candidate.effective > KR_POLICY_MODEL ||
      candidate.model_ready > 1 || error > static_cast<uint32_t>(vocabulary::Error::Cancelled) ||
      configuration_error > static_cast<uint32_t>(vocabulary::Error::Cancelled) ||
      en > static_cast<uint32_t>(vocabulary::Status::Retained) ||
      ru > static_cast<uint32_t>(vocabulary::Status::Retained) ||
      en_error > static_cast<uint32_t>(vocabulary::Error::Cancelled) ||
      ru_error > static_cast<uint32_t>(vocabulary::Error::Cancelled))
    return false;
  candidate.error = static_cast<vocabulary::Error>(error);
  candidate.configuration_error = static_cast<vocabulary::Error>(configuration_error);
  candidate.sources = {static_cast<vocabulary::Status>(en), static_cast<vocabulary::Status>(ru)};
  candidate.source_errors = {static_cast<vocabulary::Error>(en_error),
                             static_cast<vocabulary::Error>(ru_error)};
  wchar_t canonical[status_capacity]{};
  if (!Format(candidate, canonical) || text != canonical)
    return false;
  value = candidate;
  return true;
} catch (...) {
  return false;
}
inline HRESULT Read(ITfThreadMgr *manager, Snapshot &value) try {
  Microsoft::WRL::ComPtr<ITfFunctionProvider> provider;
  Microsoft::WRL::ComPtr<IUnknown> function;
  Microsoft::WRL::ComPtr<Reader> reader;
  HRESULT result = manager ? manager->GetFunctionProvider(service_id, &provider) : E_POINTER;
  if (result == S_OK && provider)
    result = provider->GetFunction(GUID_NULL, reader_iid, &function);
  if (result == S_OK && function)
    result = function->QueryInterface(reader_iid, reinterpret_cast<void **>(reader.GetAddressOf()));
  return result == S_OK && reader ? reader->Copy(sizeof(value), version, &value)
         : result == S_OK         ? E_NOINTERFACE
                                  : result;
} catch (...) {
  return E_FAIL;
}
} // namespace keyrill::runtime
