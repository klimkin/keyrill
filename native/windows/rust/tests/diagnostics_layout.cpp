#include "../../host_probe.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <io.h>

static_assert(sizeof(keyrill::host::Identity) == 40);
static_assert(sizeof(keyrill::host::Metadata) == 40);
static_assert(sizeof(keyrill::diagnostics::Snapshot) == 220);
static_assert(offsetof(keyrill::diagnostics::Snapshot, counts) == 36);
static_assert(offsetof(keyrill::diagnostics::Snapshot, first) == 188);
static_assert(sizeof(keyrill::diagnostics::LifecycleEvent) == 20);
static_assert(sizeof(keyrill::diagnostics::LifecycleSnapshot) == 344);
static_assert(sizeof(keyrill::diagnostics::SnapshotV2) == 564);
static_assert(offsetof(keyrill::diagnostics::SnapshotV2, lifecycle) == 220);
static_assert(sizeof(keyrill::runtime::Snapshot) == 48);
static_assert(offsetof(keyrill::runtime::Snapshot, revision) == 16);
static_assert(sizeof(keyrill::probe::Page) == 2092);
static_assert(offsetof(keyrill::probe::Page, words) == 44);

int main(int argc, char **argv) {
  if (argc != 4)
    return 2;
  if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1)
    return 3;
  keyrill::host::Identity identity;
  identity.process = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
  identity.started = std::strtoull(argv[2], nullptr, 10);
  identity.thread = static_cast<DWORD>(std::strtoul(argv[3], nullptr, 10));
  keyrill::probe::ArmedPage page;
  if (!page.Create(identity))
    return 4;
  if (std::putchar('R') == EOF || std::fflush(stdout))
    return 5;
  while (std::getchar() == 'R') {
    keyrill::probe::Payload payload{};
    if (page.Read(payload) != keyrill::probe::Availability::Available)
      return 6;
    keyrill::probe::Snapshot decoded;
    if (!keyrill::probe::Decode(payload, decoded))
      return 7;
    const auto &service = decoded.service;
    const auto &runtime = decoded.runtime;
    const auto &metadata = decoded.metadata;
    if (decoded.instance != 1 || service.base.activation != 7 || service.base.active != 1 ||
        service.base.mode != 2 ||
        service.base.counts[static_cast<size_t>(keyrill::diagnostics::Counter::Verified)] != 1 ||
        service.base.first.stage != keyrill::diagnostics::Stage::Request ||
        service.base.first.cause != keyrill::diagnostics::Cause::Native ||
        service.base.first.result != static_cast<uint32_t>(E_FAIL) ||
        service.lifecycle.sequence != 2 || service.lifecycle.allocated != 1 ||
        service.lifecycle.events[1].kind != keyrill::diagnostics::LifecycleKind::EndRequested ||
        runtime.activation != 7 || runtime.requested != KR_POLICY_MODEL ||
        runtime.effective != KR_POLICY_MODEL || runtime.model_ready != 1 ||
        runtime.revision != 0x123456789abcdef0ull ||
        runtime.error != keyrill::vocabulary::Error::Unavailable ||
        runtime.configuration_error != keyrill::vocabulary::Error::Configuration ||
        runtime.sources[0] != keyrill::vocabulary::Status::Ready ||
        runtime.sources[1] != keyrill::vocabulary::Status::Retained ||
        runtime.source_errors[1] != keyrill::vocabulary::Error::Read ||
        metadata.reason != keyrill::host::Reason::Session ||
        metadata.focus != keyrill::host::Focus::Exact ||
        metadata.scope != keyrill::host::Scope::Text || !metadata.entered || !metadata.completed ||
        !metadata.selection || !metadata.acp)
      return 10;
    identity.kind = decoded.kind;
    std::copy(decoded.version.begin(), decoded.version.end(), identity.version.begin());
    const auto roundtrip =
        keyrill::probe::Encode(identity, decoded.metadata, decoded.service, decoded.runtime,
                               decoded.instance, decoded.observations);
    if (roundtrip != payload)
      return 8;
    if (std::fwrite(roundtrip.data(), sizeof(DWORD), roundtrip.size(), stdout) !=
            roundtrip.size() ||
        std::fflush(stdout))
      return 9;
  }
  return 0;
}
