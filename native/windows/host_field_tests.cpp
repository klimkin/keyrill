#include "host_field_internal.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace keyrill::field;
namespace {
void Require(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
struct Provider {
  keyrill::host::Identity target{123, 0, 100, keyrill::host::Kind::ProofEditor};
  detail::LifetimeFacts lifetime{123, 100, WAIT_TIMEOUT, 123, 456, 200, WAIT_TIMEOUT};
  detail::LifetimeFacts focused_lifetime{123, 100, WAIT_TIMEOUT, 123, 789, 300, WAIT_TIMEOUT};
  detail::QueueFocus foreground_queue;
  detail::QueueFocus focused_queue;
  Metadata metadata;
  BOOL compared = TRUE;
  std::wstring text;
  bool null_text = false;
  detail::Selection selection{true, 1, 0, 0};
  Refusal read_failure = Refusal::None;
  unsigned observations = 0;
  unsigned reads = 0;
  unsigned selections = 0;
  int bound = 0;
  std::function<void(Provider &)> on_observe;
  Provider() {
    metadata.refusal = Refusal::None;
    metadata.native.pid = 123;
    metadata.native.focused.handle = reinterpret_cast<HWND>(uintptr_t{1});
    foreground_queue = focused_queue = {true, metadata.native.focused.handle};
    metadata.native.foreground = metadata.native.root.handle = reinterpret_cast<HWND>(uintptr_t{2});
    metadata.native.focused.complete = metadata.native.root.complete = true;
    metadata.native.focused.enabled = metadata.native.root.enabled = true;
    metadata.provider_pid = 123;
    metadata.provider_window = metadata.native.focused.handle;
    metadata.control_type = UIA_DocumentControlTypeId;
    metadata.nonpassword = metadata.enabled = metadata.text_pattern = true;
    metadata.value_readonly = ReadOnly::Writable;
    metadata.text_readonly = ReadOnly::Writable;
  }
  Metadata MetadataNow() {
    ++observations;
    if (on_observe)
      on_observe(*this);
    auto result = metadata;
    if (!detail::Current(lifetime, target, 456, 200) ||
        !detail::Current(focused_lifetime, target, 789, 300))
      result.refusal = Refusal::Stale;
    if (!detail::ExactQueues(foreground_queue, focused_queue, result.native.root.handle,
                             result.native.foreground))
      result.refusal = Refusal::NativeFocus;
    result.exact_binding =
        detail::Bound(result.native, result.provider_pid, result.provider_window, compared);
    return result;
  }
  Refusal ReadDocument(int limit, std::wstring &output) {
    ++reads;
    bound = limit;
    const auto length = static_cast<UINT>((std::min)(text.size(), static_cast<size_t>(limit)));
    BSTR value = null_text ? nullptr : SysAllocStringLen(text.data(), length);
    Require(null_text || value, "test BSTR allocation succeeds");
    const auto result = detail::CopyDocumentText(S_OK, value, limit, output);
    SysFreeString(value);
    return read_failure == Refusal::None ? result : read_failure;
  }
  Refusal EndCaret() {
    ++selections;
    return detail::EndCaret(selection) ? Refusal::None : Refusal::Selection;
  }
};
void RefusedWithoutRead(Provider &provider, Refusal expected, const char *message) {
  const auto result = detail::Check(provider, detail::cases[0]);
  Require(!result.matched && result.refusal == expected && !provider.reads && !provider.selections,
          message);
}
void MetadataOnly() {
  Provider provider;
  provider.text = L"private text must not be read";
  const auto result = detail::Observe(provider);
  Require(result.refusal == Refusal::None && provider.observations == 1 && !provider.reads &&
              !provider.selections,
          "metadata observation makes zero character or selection reads");
}
void IdentityAndExclusions() {
  for (unsigned schedule = 0; schedule != 7; ++schedule) {
    Provider provider;
    switch (schedule) {
    case 0:
      provider.foreground_queue.available = false;
      break;
    case 1:
      provider.focused_queue.available = false;
      break;
    case 2:
      provider.foreground_queue.focused = nullptr;
      break;
    case 3:
      provider.focused_queue.focused = nullptr;
      break;
    case 4:
      provider.focused_queue.focused = reinterpret_cast<HWND>(uintptr_t{3});
      break;
    case 5:
      provider.metadata.native.root.handle = nullptr;
      break;
    case 6:
      provider.metadata.native.root.handle = reinterpret_cast<HWND>(uintptr_t{3});
      break;
    }
    RefusedWithoutRead(
        provider, Refusal::NativeFocus,
        "both GUI queues must report the exact field under the actual foreground root");
  }
  for (unsigned schedule = 0; schedule != 16; ++schedule) {
    Provider provider;
    auto &lifetime = schedule < 8 ? provider.lifetime : provider.focused_lifetime;
    switch (schedule % 8) {
    case 0:
      lifetime.process_wait = WAIT_OBJECT_0;
      break;
    case 1:
      lifetime.thread_wait = WAIT_OBJECT_0;
      break;
    case 2:
      lifetime.thread_wait = WAIT_FAILED;
      break;
    case 3:
      ++lifetime.process;
      break;
    case 4:
      ++lifetime.process_started;
      break;
    case 5:
      ++lifetime.thread_owner;
      break;
    case 6:
      ++lifetime.thread;
      break;
    case 7:
      ++lifetime.thread_started;
      break;
    }
    RefusedWithoutRead(provider, Refusal::Stale,
                       "dead, replaced or unknown lifetime reads nothing");
  }
  for (unsigned schedule = 0; schedule != 4; ++schedule) {
    Provider provider;
    switch (schedule) {
    case 0:
      provider.metadata.provider_window = nullptr;
      break;
    case 1:
      provider.metadata.provider_window = reinterpret_cast<HWND>(uintptr_t{2});
      break;
    case 2:
      ++provider.metadata.provider_pid;
      break;
    case 3:
      provider.compared = FALSE;
      break;
    }
    RefusedWithoutRead(provider, Refusal::Binding,
                       "windowless or inexact UIA binding reads nothing");
  }
  for (auto reason : {Refusal::Target, Refusal::NativeFocus, Refusal::Stale, Refusal::Provider}) {
    Provider provider;
    provider.metadata.refusal = reason;
    RefusedWithoutRead(provider, reason, "background or failed focus/provider reads nothing");
  }
  for (unsigned schedule = 0; schedule != 6; ++schedule) {
    Provider provider;
    switch (schedule) {
    case 0:
      provider.metadata.native.focused.style = ES_PASSWORD;
      break;
    case 1:
      provider.metadata.native.focused.style = ES_READONLY;
      break;
    case 2:
      provider.metadata.native.focused.enabled = false;
      break;
    case 3:
      provider.metadata.native.root.enabled = false;
      break;
    case 4:
      provider.metadata.nonpassword = false;
      break;
    case 5:
      provider.metadata.enabled = false;
      break;
    }
    RefusedWithoutRead(provider,
                       schedule < 4    ? Refusal::NativeExcluded
                       : schedule == 4 ? Refusal::Password
                                       : Refusal::Disabled,
                       "native and provider exclusion vetoes precede text");
  }
  for (unsigned schedule = 0; schedule != 4; ++schedule) {
    Provider provider;
    switch (schedule) {
    case 0:
      provider.metadata.native.focused.complete = false;
      break;
    case 1:
      provider.metadata.native.root.complete = false;
      break;
    case 2:
      provider.metadata.native.parents_truncated = true;
      break;
    case 3:
      provider.metadata.native.parent_count = 1;
      break;
    }
    RefusedWithoutRead(provider, Refusal::NativeFacts, "incomplete native metadata reads nothing");
  }
}
void Writability() {
  Require(detail::Boolean(S_OK, FALSE) == ReadOnly::Writable &&
              detail::Boolean(S_OK, TRUE) == ReadOnly::ReadOnly &&
              detail::Boolean(S_OK, 2) == ReadOnly::Unknown &&
              detail::Boolean(E_FAIL, FALSE) == ReadOnly::Unavailable &&
              detail::Boolean(S_FALSE, FALSE) == ReadOnly::Unavailable &&
              detail::AttributeBoolean(VARIANT_FALSE) == ReadOnly::Writable &&
              detail::AttributeBoolean(VARIANT_TRUE) == ReadOnly::ReadOnly &&
              detail::AttributeBoolean(1) == ReadOnly::Unknown,
          "only exact successful native and VARIANT Boolean false establishes writability");
  for (auto value : {ReadOnly::Unavailable, ReadOnly::Unsupported, ReadOnly::Writable})
    for (auto text : {ReadOnly::Unavailable, ReadOnly::Unsupported, ReadOnly::Writable}) {
      Provider provider;
      provider.metadata.value_readonly = value;
      provider.metadata.text_readonly = text;
      const bool writable = value == ReadOnly::Writable || text == ReadOnly::Writable;
      const auto result = detail::Check(provider, detail::cases[0]);
      Require(result.matched == writable && provider.reads == (writable ? 1u : 0u),
              "at least one exact writable channel is required; native style is not positive");
    }
  for (auto veto : {ReadOnly::Unknown, ReadOnly::ReadOnly, ReadOnly::Mixed})
    for (bool value_channel : {false, true}) {
      Provider provider;
      (value_channel ? provider.metadata.value_readonly : provider.metadata.text_readonly) = veto;
      RefusedWithoutRead(provider,
                         veto == ReadOnly::ReadOnly ? Refusal::ReadOnly
                         : veto == ReadOnly::Mixed  ? Refusal::Mixed
                                                    : Refusal::Writability,
                         "unknown, mixed or conflicting evidence cannot become writable false");
    }
  Provider missing;
  missing.metadata.text_pattern = false;
  RefusedWithoutRead(missing, Refusal::TextUnavailable, "Value-only evidence cannot read text");
  for (auto control : std::array<int, 3>{0, UIA_EditControlTypeId, UIA_ButtonControlTypeId}) {
    Provider other;
    other.metadata.control_type = control;
    RefusedWithoutRead(other, Refusal::ControlType,
                       "unknown and non-document focused controls precede character access");
  }
}
void Revalidation() {
  Provider before_read;
  before_read.on_observe = [](Provider &provider) {
    if (provider.observations == 2)
      provider.lifetime.thread_wait = WAIT_OBJECT_0;
  };
  RefusedWithoutRead(before_read, Refusal::Stale, "provider-induced thread loss precedes text");
  Provider focus_drift;
  focus_drift.on_observe = [](Provider &provider) {
    if (provider.observations == 2)
      ++provider.metadata.native.focused.control;
  };
  RefusedWithoutRead(focus_drift, Refusal::Stale,
                     "provider-induced native focus drift precedes text");
  for (unsigned observation : {2u, 3u, 4u}) {
    Provider provider;
    provider.on_observe = [observation](Provider &source) {
      if (source.observations == observation)
        source.metadata.text_readonly = ReadOnly::ReadOnly;
    };
    const auto result = detail::Check(provider, detail::cases[0]);
    Require(!result.matched && result.refusal == Refusal::ReadOnly && provider.reads <= 1,
            "safety metadata is revalidated before text, after text and before success");
  }
}
void ExactPublicCases() {
  for (const auto &expected : detail::cases) {
    PublicCase parsed = PublicCase::Empty;
    Require(ParsePublicCase(expected.name, parsed) && parsed == expected.id,
            "every compiled public case has one parser entry");
    Provider provider;
    provider.text = expected.text;
    Require(detail::Check(provider, expected).matched && provider.reads == 1 &&
                provider.bound == static_cast<int>(expected.text.size() + 1) &&
                provider.selections == 1 && provider.observations == 4,
            "each public case reads only expected UTF16 plus one and requires the end caret");
    for (wchar_t suffix : {L'x', L'\0', L'\r'}) {
      Provider extra;
      extra.text = expected.text;
      extra.text.push_back(suffix);
      const auto result = detail::Check(extra, expected);
      Require(!result.matched && result.refusal == Refusal::TextMismatch && !extra.selections,
              "extra text including NUL and terminal CR refuses exact UTF16 comparison");
    }
  }
  Provider interior;
  interior.text = std::wstring(L"g\0bdtn ", 7);
  Require(detail::Check(interior, detail::cases[2]).refusal == Refusal::TextMismatch,
          "embedded NUL is compared as a code unit");
  Provider failed;
  failed.read_failure = Refusal::TextUnavailable;
  Require(detail::Check(failed, detail::cases[0]).refusal == Refusal::TextUnavailable &&
              failed.reads == 1 && !failed.selections,
          "failed character read does not reach selection or success");
  for (auto selection : {detail::Selection{false, 1, 0, 0}, detail::Selection{true, 0, 0, 0},
                         detail::Selection{true, 2, 0, 0}, detail::Selection{true, 1, 1, 0},
                         detail::Selection{true, 1, 0, -1}, detail::Selection{true, 1, 0, 1}}) {
    Provider provider;
    provider.selection = selection;
    const auto result = detail::Check(provider, detail::cases[0]);
    Require(!result.matched && result.refusal == Refusal::Selection && provider.reads == 1,
            "failed endpoints, multiple selections, noncollapsed or wrong caret refuse");
  }
}
void NullTextResult() {
  Provider provider;
  provider.null_text = true;
  const auto result = detail::Check(provider, detail::cases[0]);
  if (result.matched || result.refusal != Refusal::TextUnavailable) {
    fprintf(stderr,
            "FAIL: S_OK/null BSTR expected TextUnavailable and matched=false; actual %s and "
            "matched=%s\n",
            refusal_labels[static_cast<size_t>(result.refusal)], result.matched ? "true" : "false");
    std::exit(1);
  }
  Require(provider.reads == 1 && !provider.selections,
          "null text result cannot reach the selection check");
}
} // namespace
int main() {
  MetadataOnly();
  IdentityAndExclusions();
  Writability();
  Revalidation();
  ExactPublicCases();
  NullTextResult();
  puts("focused field evidence schedules passed");
}
