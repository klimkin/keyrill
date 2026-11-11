#include "host_probe.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace keyrill;
namespace {
void Require(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
host::Identity Current() {
  host::Identity id;
  Require(host::Capture(GetCurrentProcess(), GetCurrentProcessId(), GetCurrentThreadId(), id),
          "current x64 identity captured");
  return id;
}
probe::Payload Payload(const host::Identity &id, DWORD instance, DWORD serial = 1) {
  diagnostics::SnapshotV2 service;
  service.base.activation = serial;
  service.base.active = 1;
  runtime::Snapshot runtime;
  runtime.activation = serial;
  host::Metadata metadata;
  metadata.result = serial;
  return probe::Encode(id, metadata, service, runtime, instance, {});
}
int Child(const wchar_t *pid_text, const wchar_t *started_text, const wchar_t *tid_text) {
  const DWORD pid = wcstoul(pid_text, nullptr, 10);
  const auto started = _wcstoui64(started_text, nullptr, 16);
  const DWORD tid = wcstoul(tid_text, nullptr, 10);
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
  host::Identity id;
  Require(process && host::Capture(process, pid, tid, id) && id.started == started &&
              WaitForSingleObject(process, 0) == WAIT_TIMEOUT,
          "reader independently validates live target creation identity");
  wchar_t name[128]{};
  Require(probe::Name(id, name), "reader mapping name");
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
  Require(mapping != nullptr, "separate process opens only read access");
  const auto *page = static_cast<const probe::Page *>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, probe::page_bytes));
  Require(page != nullptr, "separate process maps read-only page");
  MEMORY_BASIC_INFORMATION information{};
  Require(VirtualQuery(page, &information, sizeof(information)) == sizeof(information) &&
              information.Protect == PAGE_READONLY,
          "actual reader page protection is read-only");
  probe::Payload words;
  probe::Snapshot decoded;
  Require(probe::Read(*page, id, words) == probe::Availability::Available &&
              probe::Decode(words, decoded) && decoded.metadata.result == 37 &&
              decoded.service.base.activation == 37 && decoded.runtime.activation == 37,
          "separate read-only process obtains coherent production payload");
  UnmapViewOfFile(page);
  CloseHandle(mapping);
  CloseHandle(process);
  return 0;
}
void ReaderProcess(const host::Identity &id) {
  wchar_t executable[32768]{};
  Require(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))) != 0,
          "reader executable path");
  wchar_t command[33000]{};
  Require(swprintf_s(command, L"\"%ls\" --reader %lu %016llx %lu", executable, id.process,
                     id.started, id.thread) > 0,
          "reader command");
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION child{};
  Require(CreateProcessW(executable, command, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                         nullptr, &startup, &child) != FALSE,
          "independent reader launched");
  Require(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0,
          "independent reader terminates");
  DWORD code = 1;
  Require(GetExitCodeProcess(child.hProcess, &code) && code == 0,
          "independent read-only reader passes");
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
}
void ObservationCodecSchedules(const host::Identity &id) {
  using namespace diagnostics;
  const ModifierFacts exact{true, true, false, false, false, false};
  LifecycleJournal lifecycle;
  const auto serial = lifecycle.Acquire();
  const ObservationStamp stamp{{lifecycle.sequence()}, {true, serial}};
  ObservationJournal journal;
  const auto command = journal.Begin(PreservedIntent{ManualIntent::Convert}, stamp);
  journal.Record(command, ExecutorDispatch{exact}, stamp);
  journal.Record(command, CommandException{exact}, stamp);
  journal.Record({ReleaseDelivery::Actual, ReleaseModifier::RightAlt}, stamp);
  SnapshotV2 service;
  service.base.activation = service.base.active = 1;
  service.lifecycle = lifecycle.Copy(true, serial);
  const auto encoded = probe::Encode(id, {}, service, {}, 1, journal.Copy());
  probe::Snapshot decoded;
  Require(probe::Decode(encoded, decoded) && decoded.observations == journal.Copy(),
          "explicit protocol2 codec preserves typed command grammar and ownership");
  const auto preserved = probe::Encode(id, decoded.metadata, decoded.service, decoded.runtime,
                                       decoded.instance, decoded.observations);
  Require(preserved == encoded && encoded[177] == 4 && encoded[179] == 4 && encoded[184] == 1 &&
              encoded[194] == 5 && encoded[204] == 6 && encoded[214] == 7,
          "protocol2 has authoritative ten-DWORD rows after frozen 177 words");
  auto invalid_domain = journal.Copy();
  std::get<CommandObservation>(invalid_domain.events[0]->body).command.intent =
      static_cast<ManualIntent>(-1);
  Require(!Valid(invalid_domain, service.lifecycle), "domain rejects negative command intent");
  invalid_domain = journal.Copy();
  std::get<CommandObservation>(invalid_domain.events[0]->body).command.route =
      static_cast<CommandRoute>(-1);
  Require(!Valid(invalid_domain, service.lifecycle), "domain rejects invalid command route");
  invalid_domain = journal.Copy();
  std::get<ReleaseObservation>(invalid_domain.events[3]->body).delivery =
      static_cast<ReleaseDelivery>(-1);
  Require(!Valid(invalid_domain, service.lifecycle), "domain rejects invalid release delivery");
  invalid_domain = journal.Copy();
  std::get<ReleaseObservation>(invalid_domain.events[3]->body).modifier =
      static_cast<ReleaseModifier>(-1);
  Require(!Valid(invalid_domain, service.lifecycle), "domain rejects invalid release modifier");
  for (const auto &[field, value] : std::initializer_list<std::pair<size_t, DWORD>>{
           {177, 0},     {178, 1}, {178, 2}, {179, 33},    {180, 0}, {180, 2},     {181, 2},
           {182, 2},     {183, 2}, {184, 0}, {184, 8},     {185, 0}, {185, 3},     {186, 0},
           {186, 3},     {187, 0}, {187, 2}, {188, 0x100}, {188, 1}, {188, 0x140}, {189, 1},
           {194, 3},     {194, 2}, {195, 1}, {196, 2},     {197, 2}, {198, 0},     {198, 0x100},
           {198, 0x203}, {204, 5}, {208, 0}, {208, 0x102}, {215, 1}, {216, 1},     {217, 1},
           {218, 0},     {218, 7}, {219, 0}, {219, 3},     {220, 1}, {500, 1},     {511, 1}}) {
    auto malformed = encoded;
    malformed[field] = value;
    auto unchanged = decoded;
    Require(!probe::Decode(malformed, unchanged) &&
                probe::Encode(id, unchanged.metadata, unchanged.service, unchanged.runtime,
                              unchanged.instance, unchanged.observations) == encoded,
            "hostile observation field is rejected without exposing partial output");
  }
  ObservationJournal incomplete;
  const auto incomplete_ticket = incomplete.Begin(PreservedIntent{ManualIntent::Reverse}, {});
  auto words = probe::Encode(id, {}, {}, {}, 1, incomplete.Copy());
  Require(probe::Decode(words, decoded), "incomplete preserved arrival remains decodable");
  incomplete.Record(incomplete_ticket, CommandException{ModifierFacts{}}, {});
  words = probe::Encode(id, {}, {}, {}, 1, incomplete.Copy());
  Require(probe::Decode(words, decoded) &&
              std::holds_alternative<ModifierFacts>(Knowledge(
                  std::get<CommandObservation>(decoded.observations.events[1]->body).stage)),
          "known all-zero facts differ from unknown acquisition knowledge");
  ObservationJournal overflow;
  const auto retained_ticket = overflow.Begin(PreservedIntent{ManualIntent::Convert}, {});
  for (size_t i = 0; i != observation_capacity; ++i)
    overflow.Record({ReleaseDelivery::Test, ReleaseModifier::LeftControl}, {});
  overflow.Record(retained_ticket, ExecutorDispatch{exact}, {});
  Require(Valid(overflow.Copy(), {}) && overflow.Copy().count == observation_capacity &&
              overflow.Copy().events[0]->sequence.value == 3 &&
              std::get<CommandObservation>(overflow.Copy().events.back()->body)
                      .command.id.arrival.value == 1,
          "evicted arrivals retain command identity and contiguous visible history");
  ObservationJournal saturated(UINT32_MAX - 33);
  for (size_t i = 0; i != 33; ++i)
    saturated.Record({ReleaseDelivery::Actual, ReleaseModifier::AltUnresolved}, {});
  const auto frozen = saturated.Copy();
  Require(frozen.saturated && frozen.sequence.value == UINT32_MAX && Valid(frozen, {}),
          "last representable sequence is appended before saturation freezes history");
  Require(!saturated.Begin(PreservedIntent{ManualIntent::Convert}, {}).identity,
          "new saturated command tickets become inactive");
  saturated.Record(retained_ticket, ExecutorDispatch{exact}, {});
  saturated.Record({ReleaseDelivery::Test, ReleaseModifier::RightControl}, {});
  Require(saturated.Copy() == frozen, "saturation never wraps or overwrites retained history");
  Require(probe::Decode(probe::Encode(id, {}, {}, {}, 1, frozen), decoded) &&
              decoded.observations == frozen,
          "saturated history retains the final sequence through the wire codec");
  const std::array<CommandProgress, 5> progress{
      AdmissionRefused{exact}, ExactModifiersRefused{ModifierFacts{}}, DispatchRefused{exact},
      ExecutorDispatch{exact}, CommandException{exact}};
  for (bool ordinary : {false, true}) {
    for (size_t first = 0; first != progress.size(); ++first) {
      for (size_t second = 0; second <= progress.size(); ++second) {
        ObservationJournal grammar;
        const RecognizedCommand recognized =
            ordinary ? RecognizedCommand{OrdinaryIntent{ManualIntent::Convert, exact}}
                     : RecognizedCommand{PreservedIntent{ManualIntent::Convert}};
        const auto ticket = grammar.Begin(recognized, {});
        grammar.Record(ticket, progress[first], {});
        if (second != progress.size())
          grammar.Record(ticket, progress[second], {});
        const bool expected =
            (!ordinary || first >= 2) && (second == progress.size() || (first == 3 && second == 4));
        Require(Valid(grammar.Copy(), {}) == expected &&
                    probe::Decode(probe::Encode(id, {}, {}, {}, 1, grammar.Copy()), decoded) ==
                        expected,
                "both routes enforce the complete terminal-progress grammar");
      }
    }
  }
  LifecycleJournal exhausted_serials(0, UINT32_MAX);
  const auto zero_serial = exhausted_serials.Acquire();
  SnapshotV2 exhausted_service;
  exhausted_service.lifecycle = exhausted_serials.Copy(true, zero_serial);
  ObservationJournal zero_owned;
  zero_owned.Record({ReleaseDelivery::Test, ReleaseModifier::LeftAlt},
                    {{exhausted_serials.sequence()}, {true, zero_serial}});
  Require(
      probe::Decode(probe::Encode(id, {}, exhausted_service, {}, 1, zero_owned.Copy()), decoded) &&
          decoded.observations.events[0]->stamp.owned.present &&
          decoded.observations.events[0]->stamp.owned.composition_serial == 0,
      "allocation saturation preserves owned-present independently from serial zero");
  probe::Snapshot before, after;
  Require(probe::Decode(probe::Encode(id, {}, service, {}, 1, {}), before) &&
              probe::Decode(encoded, after) && probe::CompleteInterval(before, after),
          "coherent same-instance build activation baseline covers both histories");
  auto mismatch = after;
  ++mismatch.instance;
  Require(!probe::CompleteInterval(before, mismatch), "changed instance has no complete interval");
  mismatch = after;
  ++mismatch.build[0];
  Require(!probe::CompleteInterval(before, mismatch), "changed build has no complete interval");
  mismatch = after;
  ++mismatch.service.base.activation;
  Require(!probe::CompleteInterval(before, mismatch),
          "changed activation has no complete interval");
  Require(probe::Decode(probe::Encode(id, {}, service, {}, 1, overflow.Copy()), mismatch) &&
              !probe::CompleteInterval(before, mismatch),
          "observation overflow prevents complete absence claims");
  mismatch = after;
  mismatch.observations.events[0]->stamp.owned.present = false;
  mismatch.observations.events[0]->stamp.owned.composition_serial = 0;
  Require(!probe::CompleteInterval(after, mismatch), "overlap cannot rewrite sampled ownership");
  for (size_t i = 0; i != lifecycle_capacity + 1; ++i)
    lifecycle.Record(LifecycleKind::LifecycleInvalidation, serial, LifecycleCause::DocumentFocus);
  service.lifecycle = lifecycle.Copy(true, serial);
  Require(probe::Decode(probe::Encode(id, {}, service, {}, 1, journal.Copy()), mismatch) &&
              !probe::CompleteInterval(before, mismatch),
          "independent lifecycle overflow prevents complete absence claims");
  std::array<DWORD, 192> protocol1_fixture{};
  protocol1_fixture[0] = 1;
  protocol1_fixture[24] = protocol1_fixture[25] = 1;
  Require(std::all_of(protocol1_fixture.begin() + 177, protocol1_fixture.end(),
                      [](DWORD word) { return word == 0; }),
          "frozen protocol1 fixture reserves fifteen zero tail words");
  probe::Payload unchanged_prefix{};
  std::copy(protocol1_fixture.begin(), protocol1_fixture.end(), unchanged_prefix.begin());
  Require(probe::Decode(unchanged_prefix, decoded) && decoded.service.base.activation == 1 &&
              decoded.service.base.active && decoded.observations.count == 0,
          "frozen protocol1 fields retain their meanings in the unchanged protocol2 prefix");
  protocol1_fixture[177] = 1;
  Require(!std::all_of(protocol1_fixture.begin() + 177, protocol1_fixture.end(),
                       [](DWORD word) { return word == 0; }),
          "assigning protocol1 reserved tail violates its frozen fixture contract");
  probe::Page legacy{};
  probe::Initialize(legacy, id);
  legacy.header[1] = 1;
  legacy.header[3] = 192;
  Require(!probe::Matches(legacy, id), "protocol3 rejects frozen protocol1 header");
  probe::Initialize(legacy, id);
  legacy.header[1] = 2;
  Require(!probe::Matches(legacy, id), "protocol3 rejects the frozen protocol2 header too");
  for (const auto outcome : {CommandCompletion::Refused, CommandCompletion::Unknown,
                             CommandCompletion::Reserved, CommandCompletion::ManualMark}) {
    for (bool witnessed : {false, true}) {
      ObservationJournal retention;
      retention.Record(ObserverObservation{ObserverStage::Arrival}, stamp);
      retention.Record(ObserverObservation{ObserverStage::Admitted}, stamp);
      const auto ticket = retention.Begin(PreservedIntent{ManualIntent::Convert}, stamp);
      retention.Record(ticket, ExecutorDispatch{exact}, stamp);
      retention.Record(CompletionObservation{*ticket.identity, witnessed, outcome}, stamp);
      retention.Record(ReleaseObservation{ReleaseDelivery::Actual, ReleaseModifier::LeftAlt,
                                          ReleaseObservation::Context::Matched, true},
                       stamp);
      const auto retained_service = service;
      const auto payload = probe::Encode(id, {}, retained_service, {}, 1, retention.Copy());
      probe::Snapshot roundtrip;
      Require(probe::Decode(payload, roundtrip) && roundtrip.observations == retention.Copy(),
              "protocol3 roundtrips observer, typed completion, witness and release disposition");
      for (const auto &[field, value] : std::initializer_list<std::pair<size_t, DWORD>>{
               {184, 0}, {185, 1}, {186, 1}, {187, 1}, {188, 0}, {188, 4}, {189, 1},
               {224, 8}, {225, 0}, {226, 0}, {227, 0}, {227, 5}, {228, 2}, {229, 0},
               {229, 5}, {235, 2}, {236, 0}, {236, 3}, {237, 1}, {238, 4}}) {
        auto malformed = payload;
        malformed[field] = value;
        const auto saved = roundtrip.observations;
        Require(!probe::Decode(malformed, roundtrip) && roundtrip.observations == saved,
                "protocol3 rejects malformed authority evidence without partial publication");
      }
      retention.Record(CompletionObservation{*ticket.identity, witnessed, outcome}, stamp);
      Require(!Valid(retention.Copy(), retained_service.lifecycle),
              "one command cannot publish a second executor completion");
    }
  }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc == 5 && wcscmp(argv[1], L"--reader") == 0)
    return Child(argv[2], argv[3], argv[4]);
  Require(argc == 1, "known diagnostic schedule mode");
  const auto id = Current();
  ObservationCodecSchedules(id);
  probe::Publisher absent;
  absent.Attach(id);
  Require(!absent.instance(), "missing arm disables publication");
  probe::ArmedPage armed;
  Require(armed.Create(id), "current logon ACL arms fresh mapping");
  probe::ArmedPage collision;
  Require(!collision.Create(id), "second arm rejects existing object without mutation");
  probe::Payload words;
  Require(armed.Read(words) == probe::Availability::Missing, "armed is not active evidence");
  probe::Publisher first, second;
  first.Attach(id);
  Require(first.instance() != 0, "first instance claims thread page");
  second.Attach(id);
  Require(second.instance() == 0, "second same-thread instance cannot overwrite publisher");
  first.Publish(Payload(id, first.instance(), 37));
  second.Publish(Payload(id, 100, 99));
  Require(armed.Read(words) == probe::Availability::Available, "production coherent publication");
  probe::Snapshot decoded;
  Require(probe::Decode(words, decoded) && decoded.metadata.result == 37,
          "colliding instance leaves owner snapshot intact");
  ReaderProcess(id);
  const auto previous_instance = first.instance();
  first.Close();
  Require(armed.Read(words) == probe::Availability::Missing,
          "retained page after publisher closes is unavailable");
  second.Attach(id);
  Require(second.instance() > previous_instance,
          "same armed page never reuses instance after unload");
  Require(armed.Read(words) != probe::Availability::Available,
          "new claim cannot expose previous instance payload");
  second.Publish(Payload(id, second.instance(), 38));
  Require(armed.Read(words) == probe::Availability::Available && probe::Decode(words, decoded) &&
              decoded.instance != previous_instance && decoded.metadata.result == 38,
          "reopened publisher is a distinct publication epoch");

  wchar_t name[128]{};
  Require(probe::Name(id, name), "negative schedule mapping name");
  HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
  auto *page = static_cast<probe::Page *>(
      MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, probe::page_bytes));
  Require(page != nullptr, "negative schedule owned mapping access");
  const auto sequence = page->sequence;
  InterlockedExchange(&page->sequence, sequence + 1);
  Require(armed.Read(words) == probe::Availability::Busy, "incomplete odd publication is busy");
  InterlockedExchange(&page->sequence, sequence);
  for (size_t field = 0; field != std::size(page->header); ++field) {
    const auto original = page->header[field];
    InterlockedExchange(&page->header[field], original + 1);
    Require(armed.Read(words) == probe::Availability::Incompatible,
            "wrong version size PID creation or TID is unavailable");
    InterlockedExchange(&page->header[field], original);
  }
  auto malformed = Payload(id, second.instance(), 38);
  malformed.back() = 1;
  second.Publish(malformed);
  Require(armed.Read(words) == probe::Availability::Incompatible,
          "noncanonical reserved fields reject malformed snapshot");
  malformed = Payload(id, second.instance(), 38);
  malformed[14] = MAXDWORD;
  second.Publish(malformed);
  Require(armed.Read(words) == probe::Availability::Incompatible,
          "unknown metadata enum is unavailable");
  std::atomic<bool> done = false;
  std::thread writer([&] {
    for (DWORD value = 1; value != 2001; ++value)
      second.Publish(Payload(id, second.instance(), value));
    done = true;
  });
  while (!done) {
    if (armed.Read(words) == probe::Availability::Available) {
      Require(probe::Decode(words, decoded) &&
                  decoded.metadata.result == decoded.service.base.activation &&
                  decoded.metadata.result == decoded.runtime.activation,
              "concurrent read never accepts a torn snapshot");
    }
  }
  writer.join();
  Require(armed.Read(words) == probe::Availability::Available, "final stable concurrent payload");
  InterlockedExchange(&page->sequence, static_cast<LONG>(MAXDWORD - 1));
  Require(armed.Read(words) == probe::Availability::Incompatible,
          "exhausted publication sequence cannot retain available evidence");
  second.Publish(Payload(id, second.instance(), 40));
  Require(armed.Read(words) == probe::Availability::Incompatible,
          "exhausted publication sequence cannot wrap");
  InterlockedExchange(&page->allocated, LONG_MAX);
  second.Close();
  first.Attach(id);
  Require(!first.instance(), "exhausted instance allocator cannot wrap or reuse identity");
  UnmapViewOfFile(page);
  CloseHandle(mapping);

  auto truncated_id = id;
  ++truncated_id.thread;
  Require(probe::Name(truncated_id, name), "truncated mapping name");
  mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 16, name);
  Require(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "fresh deliberately truncated page");
  first.Attach(truncated_id);
  Require(!first.instance(), "truncated mapping cannot attach publisher");
  CloseHandle(mapping);
  puts("Host diagnostic mapping schedules passed, including independent read-only process.");
  return 0;
}
