#pragma once
#include <cwchar>

namespace keyrill::fixture {
enum class Mode { Normal, DenyTsfLocks, ProveOwnedShutdown, ProveOwnedEnToRu, Invalid };
inline Mode ParseMode(int argc, const wchar_t *const *argv) noexcept {
  if (argc == 1 && argv)
    return Mode::Normal;
  if (argc == 2 && argv && argv[1]) {
    if (wcscmp(argv[1], L"--deny-tsf-locks") == 0)
      return Mode::DenyTsfLocks;
    if (wcscmp(argv[1], L"--prove-owned-shutdown") == 0)
      return Mode::ProveOwnedShutdown;
    if (wcscmp(argv[1], L"--prove-owned-en-to-ru") == 0)
      return Mode::ProveOwnedEnToRu;
  }
  return Mode::Invalid;
}
} // namespace keyrill::fixture
