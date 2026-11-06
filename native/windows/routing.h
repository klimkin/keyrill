#pragma once
#include <array>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <string_view>
#include <variant>
#include <windows.h>

namespace keyrill::routing {
inline constexpr size_t status_capacity = 512;
inline constexpr std::wstring_view heading = L"Keyrill host routing v1";
enum Counter : size_t {
  Down,
  TestError,
  NoInterest,
  Dispatch,
  DispatchError,
  Passed,
  Eaten,
  Count
};
inline constexpr std::array<const wchar_t *, Count> labels = {
    L"down", L"test_error", L"no_interest", L"dispatch", L"dispatch_error", L"passed", L"eaten"};
enum class HostCall { None, TestKeyDown, KeyDown };
struct Failure {
  HostCall call = HostCall::None;
  HRESULT result = S_OK;
};
struct Snapshot {
  uint32_t publication = 0;
  std::array<uint32_t, Count> counts{};
  Failure first_error;
};
struct TestRejected {
  HRESULT result;
  bool interest;
};
struct Dispatched {
  HRESULT result;
  bool eaten;
};
using DownRoute = std::variant<TestRejected, Dispatched>;

template <typename Test, typename DispatchKey>
DownRoute RouteDown(Test test, DispatchKey dispatch) {
  BOOL interest = FALSE;
  const HRESULT tested = test(&interest);
  if (FAILED(tested) || !interest)
    return TestRejected{tested, interest != FALSE};
  BOOL eaten = FALSE;
  const HRESULT handled = dispatch(&eaten);
  return Dispatched{handled, eaten != FALSE};
}
inline bool Consumed(const DownRoute &route) noexcept {
  const auto *dispatched = std::get_if<Dispatched>(&route);
  return dispatched && SUCCEEDED(dispatched->result) && dispatched->eaten;
}
inline void Increment(uint32_t &value) noexcept {
  if (value != std::numeric_limits<uint32_t>::max())
    ++value;
}
inline void Record(Snapshot &snapshot, const DownRoute &route) noexcept {
  Increment(snapshot.counts[Down]);
  HostCall failed_call = HostCall::None;
  HRESULT failed_result = S_OK;
  if (const auto *rejected = std::get_if<TestRejected>(&route)) {
    if (FAILED(rejected->result)) {
      Increment(snapshot.counts[TestError]);
      failed_call = HostCall::TestKeyDown;
      failed_result = rejected->result;
    } else {
      Increment(snapshot.counts[NoInterest]);
    }
  } else if (const auto *dispatched = std::get_if<Dispatched>(&route)) {
    Increment(snapshot.counts[Dispatch]);
    if (FAILED(dispatched->result)) {
      Increment(snapshot.counts[DispatchError]);
      failed_call = HostCall::KeyDown;
      failed_result = dispatched->result;
    } else {
      Increment(snapshot.counts[dispatched->eaten ? Eaten : Passed]);
    }
  }
  if (snapshot.first_error.call == HostCall::None && failed_call != HostCall::None)
    snapshot.first_error = {failed_call, failed_result};
}
inline bool Saturated(const Snapshot &snapshot) noexcept {
  if (snapshot.publication == std::numeric_limits<uint32_t>::max())
    return true;
  for (uint32_t count : snapshot.counts)
    if (count == std::numeric_limits<uint32_t>::max())
      return true;
  return false;
}
inline bool Format(const Snapshot &snapshot, wchar_t (&output)[status_capacity]) noexcept {
  int written = swprintf_s(output, L"%ls; publication=%lu", heading.data(),
                           static_cast<unsigned long>(snapshot.publication));
  if (written < 0)
    return false;
  for (size_t i = 0; i != Count; ++i) {
    int added = swprintf_s(output + written, status_capacity - written, L"; %ls=%lu", labels[i],
                           static_cast<unsigned long>(snapshot.counts[i]));
    if (added < 0)
      return false;
    written += added;
  }
  int added = swprintf_s(output + written, status_capacity - written,
                         L"; saturated=%u; first_error=", Saturated(snapshot) ? 1u : 0u);
  if (added < 0)
    return false;
  written += added;
  if (snapshot.first_error.call == HostCall::None)
    return swprintf_s(output + written, status_capacity - written, L"none") >= 0;
  return swprintf_s(output + written, status_capacity - written, L"%ls/0x%08lX",
                    snapshot.first_error.call == HostCall::TestKeyDown ? L"TestKeyDown"
                                                                       : L"KeyDown",
                    static_cast<unsigned long>(snapshot.first_error.result)) >= 0;
}
inline bool Take(std::wstring_view &input, std::wstring_view expected) noexcept {
  if (!input.starts_with(expected))
    return false;
  input.remove_prefix(expected.size());
  return true;
}
inline bool Number(std::wstring_view &input, uint32_t &value) noexcept {
  value = 0;
  size_t digits = 0;
  while (digits < input.size() && input[digits] >= L'0' && input[digits] <= L'9') {
    uint32_t digit = input[digits] - L'0';
    if ((digits && input[0] == L'0') || value > (std::numeric_limits<uint32_t>::max() - digit) / 10)
      return false;
    value = value * 10 + digit;
    ++digits;
  }
  input.remove_prefix(digits);
  return digits != 0;
}
inline bool Parse(std::wstring_view input, Snapshot &snapshot) noexcept {
  if (input.empty() || input.size() >= status_capacity)
    return false;
  Snapshot parsed;
  if (!Take(input, heading) || !Take(input, L"; publication=") ||
      !Number(input, parsed.publication))
    return false;
  for (size_t i = 0; i != Count; ++i)
    if (!Take(input, L"; ") || !Take(input, labels[i]) || !Take(input, L"=") ||
        !Number(input, parsed.counts[i]))
      return false;
  if (!Take(input, L"; saturated=") || !Take(input, Saturated(parsed) ? L"1" : L"0") ||
      !Take(input, L"; first_error="))
    return false;
  if (!Take(input, L"none")) {
    if (Take(input, L"TestKeyDown/0x"))
      parsed.first_error.call = HostCall::TestKeyDown;
    else if (Take(input, L"KeyDown/0x"))
      parsed.first_error.call = HostCall::KeyDown;
    else
      return false;
    if (input.size() != 8)
      return false;
    uint32_t result = 0;
    for (wchar_t digit : input) {
      if (digit >= L'0' && digit <= L'9')
        result = result * 16 + (digit - L'0');
      else if (digit >= L'A' && digit <= L'F')
        result = result * 16 + (digit - L'A' + 10);
      else
        return false;
    }
    parsed.first_error.result = static_cast<HRESULT>(result);
    if (SUCCEEDED(parsed.first_error.result))
      return false;
    input.remove_prefix(8);
  }
  if (!input.empty())
    return false;
  snapshot = parsed;
  return true;
}
} // namespace keyrill::routing
