#include "native_field.h"
#include "test_service.h"
#include <algorithm>
#include <cstdlib>

namespace keyrill::native_field {
namespace {
KrWindowsHostIdentity CopyHost(const host::Identity &value) {
  KrWindowsHostIdentity out{};
  out.process = value.process;
  out.thread = value.thread;
  out.started = value.started;
  out.kind = static_cast<uint32_t>(value.kind);
  std::copy(value.version.begin(), value.version.end(), out.version);
  return out;
}

KrWindowsFieldWindow CopyWindow(const Window &value) {
  KrWindowsFieldWindow out{};
  out.handle = value.handle;
  out.process = value.process;
  out.thread = value.thread;
  out.control = value.control;
  out.style = value.style;
  std::copy(value.class_name.begin(), value.class_name.end(), out.class_name);
  out.enabled = value.enabled;
  out.complete = value.complete;
  return out;
}

Window CopyWindow(const KrWindowsFieldWindow &value) {
  Window out{};
  out.handle = value.handle;
  out.process = value.process;
  out.thread = value.thread;
  out.control = value.control;
  out.style = value.style;
  std::copy(std::begin(value.class_name), std::end(value.class_name), out.class_name.begin());
  out.enabled = value.enabled != 0;
  out.complete = value.complete != 0;
  return out;
}
} // namespace

bool KnownNotepad(const host::Identity &identity) noexcept {
  auto known = reinterpret_cast<HRESULT(WINAPI *)(const KrWindowsHostIdentity *, BOOL *)>(
      TestExport("KeyrillTestKnownNotepad"));
  const auto input = CopyHost(identity);
  BOOL result = FALSE;
  if (known(&input, &result) != S_OK)
    std::abort();
  return result != FALSE;
}

Facts Capture(HWND view, const host::Identity &identity) noexcept {
  auto capture =
      reinterpret_cast<HRESULT(WINAPI *)(HWND, const KrWindowsHostIdentity *,
                                         KrWindowsFieldFacts *)>(TestExport("KeyrillTestCapture"));
  const auto input = CopyHost(identity);
  KrWindowsFieldFacts value{};
  if (capture(view, &input, &value) != S_OK)
    std::abort();
  Facts result{};
  result.host.process = value.host.process;
  result.host.thread = value.host.thread;
  result.host.started = value.host.started;
  result.host.kind = static_cast<host::Kind>(value.host.kind);
  std::copy(std::begin(value.host.version), std::end(value.host.version),
            result.host.version.begin());
  result.process = value.process;
  result.thread = value.thread;
  result.view = value.view;
  result.focus = value.focus;
  result.foreground = value.foreground;
  result.root = value.root;
  result.field = CopyWindow(value.field);
  result.parent = CopyWindow(value.parent);
  result.grandparent = CopyWindow(value.grandparent);
  result.proof_root = CopyWindow(value.proof_root);
  result.proof_process = value.proof_process != 0;
  return result;
}

std::optional<FieldIdentity> Match(const Facts &facts) noexcept {
  KrWindowsFieldFacts input{};
  input.host = CopyHost(facts.host);
  input.process = facts.process;
  input.thread = facts.thread;
  input.view = facts.view;
  input.focus = facts.focus;
  input.foreground = facts.foreground;
  input.root = facts.root;
  input.field = CopyWindow(facts.field);
  input.parent = CopyWindow(facts.parent);
  input.grandparent = CopyWindow(facts.grandparent);
  input.proof_root = CopyWindow(facts.proof_root);
  input.proof_process = facts.proof_process;
  KrWindowsFieldIdentity output{};
  BOOL matched = FALSE;
  auto match =
      reinterpret_cast<HRESULT(WINAPI *)(const KrWindowsFieldFacts *, KrWindowsFieldIdentity *,
                                         BOOL *)>(TestExport("KeyrillTestMatch"));
  if (match(&input, &output, &matched) != S_OK)
    std::abort();
  if (!matched)
    return std::nullopt;
  return FieldIdentity{static_cast<Kind>(output.kind),
                       output.field,
                       output.parent,
                       output.root,
                       output.process,
                       output.thread,
                       output.root_thread};
}
} // namespace keyrill::native_field
