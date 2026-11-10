#pragma once
#include <array>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <windows.h>

namespace keyrill::inspection {
using BuildDigest = std::array<uint32_t, 8>;
enum class Command { Build, Host, Service, PublicEmpty };
struct Options {
  Command command = Command::Build;
  DWORD pid = 0;
  DWORD seconds = 0;
  std::optional<BuildDigest> expected_build;
};
inline bool Number(const wchar_t *text, DWORD &value) noexcept {
  if (!text || !*text)
    return false;
  DWORD candidate = 0;
  for (; *text; ++text) {
    if (*text < L'0' || *text > L'9')
      return false;
    const DWORD digit = *text - L'0';
    if (candidate > (MAXDWORD - 1 - digit) / 10)
      return false;
    candidate = candidate * 10 + digit;
  }
  if (!candidate)
    return false;
  value = candidate;
  return true;
}
inline bool Digest(const wchar_t *text, BuildDigest &output) noexcept {
  if (!text || wcslen(text) != 64)
    return false;
  BuildDigest candidate{};
  for (size_t i = 0; i != 64; ++i) {
    const auto character = text[i];
    uint32_t digit = 0;
    if (character >= L'0' && character <= L'9')
      digit = character - L'0';
    else if (character >= L'a' && character <= L'f')
      digit = character - L'a' + 10;
    else if (character >= L'A' && character <= L'F')
      digit = character - L'A' + 10;
    else
      return false;
    candidate[i / 8] = (candidate[i / 8] << 4) | digit;
  }
  output = candidate;
  return true;
}
inline bool ParseOptions(int argc, const wchar_t *const *argv, Options &output) noexcept {
  Options candidate;
  if (argc == 2 && wcscmp(argv[1], L"build") == 0) {
    output = candidate;
    return true;
  }
  if (argc < 3 || argc > 7 || argc % 2 == 0 || !Number(argv[2], candidate.pid))
    return false;
  if (wcscmp(argv[1], L"host") == 0)
    candidate.command = Command::Host;
  else if (wcscmp(argv[1], L"service") == 0)
    candidate.command = Command::Service;
  else if (wcscmp(argv[1], L"public-empty") == 0)
    candidate.command = Command::PublicEmpty;
  else
    return false;
  if (argc != 3 && candidate.command != Command::Service)
    return false;
  for (int i = 3; i < argc; i += 2) {
    if (wcscmp(argv[i], L"--arm-seconds") == 0 && !candidate.seconds) {
      if (!Number(argv[i + 1], candidate.seconds) || candidate.seconds > 3600)
        return false;
    } else if (wcscmp(argv[i], L"--expect-server-build") == 0 && !candidate.expected_build) {
      BuildDigest digest;
      if (!Digest(argv[i + 1], digest))
        return false;
      candidate.expected_build = digest;
    } else {
      return false;
    }
  }
  output = candidate;
  return true;
}
} // namespace keyrill::inspection
