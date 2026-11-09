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
  return probe::Encode(id, metadata, service, runtime, instance);
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
} // namespace
int wmain(int argc, wchar_t **argv) {
  if (argc == 5 && wcscmp(argv[1], L"--reader") == 0)
    return Child(argv[2], argv[3], argv[4]);
  Require(argc == 1, "known diagnostic schedule mode");
  const auto id = Current();
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
