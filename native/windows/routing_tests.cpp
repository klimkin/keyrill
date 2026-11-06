#include "routing.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
void Check(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
using namespace keyrill::routing;
void Route(HRESULT tested, BOOL interest, HRESULT handled, BOOL eaten, bool dispatched,
           bool consumed, Counter counter) {
  int calls = 0;
  auto route = RouteDown(
      [&](BOOL *output) {
        Check(calls++ == 0 && *output == FALSE, "TestKeyDown must be first with a false output");
        *output = interest;
        return tested;
      },
      [&](BOOL *output) {
        Check(calls++ == 1 && *output == FALSE, "KeyDown must be second with a fresh false output");
        *output = eaten;
        return handled;
      });
  Check(calls == (dispatched ? 2 : 1), "Wrong routing call count");
  Check(Consumed(route) == consumed, "Wrong ordinary-dispatch disposition");
  Snapshot snapshot;
  Record(snapshot, route);
  for (size_t i = 0; i != Count; ++i) {
    uint32_t expected = i == Down || i == counter || (dispatched && i == Dispatch) ? 1u : 0u;
    Check(snapshot.counts[i] == expected, "Wrong completed-outcome counters");
  }
  Check(snapshot.publication == 0, "Recording must not publish");
  HostCall failure = FAILED(tested)                  ? HostCall::TestKeyDown
                     : FAILED(handled) && dispatched ? HostCall::KeyDown
                                                     : HostCall::None;
  Check(snapshot.first_error.call == failure, "Wrong first failed call");
  if (failure != HostCall::None)
    Check(snapshot.first_error.result == (FAILED(tested) ? tested : handled),
          "Wrong first HRESULT");
}
std::wstring Text(const Snapshot &snapshot) {
  wchar_t output[status_capacity]{};
  Check(Format(snapshot, output), "Status format failed");
  return output;
}
void Rejected(const std::wstring &text) {
  Snapshot output;
  output.publication = 42;
  Check(!Parse(text, output), "Malformed status accepted");
  Check(output.publication == 42, "Failed parse changed the snapshot");
}
void Replaced(const std::wstring &valid, std::wstring_view before, std::wstring_view after) {
  auto changed = valid;
  auto at = changed.find(before);
  Check(at != std::wstring::npos, "Missing negative-test token");
  changed.replace(at, before.size(), after);
  Rejected(changed);
}
void RoundTrip(const Snapshot &snapshot) {
  Snapshot parsed;
  auto text = Text(snapshot);
  Check(Parse(text, parsed) && Text(parsed) == text, "Status did not round-trip canonically");
  Check(parsed.counts == snapshot.counts && parsed.publication == snapshot.publication &&
            parsed.first_error.call == snapshot.first_error.call &&
            parsed.first_error.result == snapshot.first_error.result,
        "Round-trip changed known fields");
}
} // namespace
int main() {
  Route(E_FAIL, FALSE, S_OK, FALSE, false, false, TestError);
  Route(E_ACCESSDENIED, 7, S_OK, TRUE, false, false, TestError);
  Route(S_OK, FALSE, E_FAIL, TRUE, false, false, NoInterest);
  Route(S_FALSE, FALSE, E_FAIL, TRUE, false, false, NoInterest);
  Route(S_OK, TRUE, E_FAIL, FALSE, true, false, DispatchError);
  Route(S_FALSE, 7, E_ACCESSDENIED, 7, true, false, DispatchError);
  Route(S_OK, TRUE, S_OK, FALSE, true, false, Passed);
  Route(S_FALSE, 7, S_FALSE, FALSE, true, false, Passed);
  Route(S_OK, TRUE, S_OK, TRUE, true, true, Eaten);
  Route(S_FALSE, 7, S_FALSE, 7, true, true, Eaten);

  Snapshot snapshot;
  auto zero = Text(snapshot);
  Check(zero == L"Keyrill host routing v1; publication=0; down=0; test_error=0; no_interest=0; "
                L"dispatch=0; dispatch_error=0; passed=0; eaten=0; saturated=0; first_error=none",
        "Unexpected initial schema");
  RoundTrip(snapshot);
  Record(snapshot, TestRejected{E_ACCESSDENIED, true});
  Record(snapshot, Dispatched{E_FAIL, true});
  Record(snapshot, TestRejected{S_FALSE, false});
  Check(snapshot.first_error.call == HostCall::TestKeyDown &&
            snapshot.first_error.result == E_ACCESSDENIED,
        "Later events replaced the first failure");
  RoundTrip(snapshot);
  Snapshot dispatch_failure;
  Record(dispatch_failure, Dispatched{E_FAIL, true});
  RoundTrip(dispatch_failure);
  Increment(snapshot.publication);
  Check(snapshot.publication == 1, "Publication sequence did not advance during a quiet interval");
  RoundTrip(snapshot);
  snapshot.counts.fill(std::numeric_limits<uint32_t>::max() - 1);
  snapshot.publication = std::numeric_limits<uint32_t>::max() - 1;
  for (int i = 0; i != 3; ++i) {
    Record(snapshot, TestRejected{E_FAIL, true});
    Record(snapshot, TestRejected{S_FALSE, false});
    Record(snapshot, Dispatched{E_FAIL, true});
    Record(snapshot, Dispatched{S_FALSE, false});
    Record(snapshot, Dispatched{S_FALSE, true});
    Increment(snapshot.publication);
  }
  for (uint32_t count : snapshot.counts)
    Check(count == std::numeric_limits<uint32_t>::max(), "Outcome counter wrapped");
  Check(snapshot.publication == std::numeric_limits<uint32_t>::max() && Saturated(snapshot),
        "Publication sequence wrapped or saturation hidden");
  RoundTrip(snapshot);
  Rejected(L"");
  Rejected(std::wstring(status_capacity, L'x'));
  Rejected(zero + L"; unknown=1");
  Rejected(zero + std::wstring(1, L'\0'));
  Replaced(zero, L"v1", L"v2");
  Replaced(zero, L"publication=0", L"publication=-1");
  Replaced(zero, L"down=0", L"down=00");
  Replaced(zero, L"down=0", L"down=4294967296");
  Replaced(zero, L"down=0", L"down=+1");
  Replaced(zero, L"down=0", L"unknown=0");
  Replaced(zero, L"; down=0", L"");
  Replaced(zero, L"; down=0", L"; down=0; down=0");
  Replaced(zero, L"saturated=0", L"saturated=1");
  Replaced(zero, L"first_error=none", L"first_error=anything");
  Replaced(zero, L"first_error=none", L"first_error=KeyDown/0x00000001");
  Replaced(zero, L"first_error=none", L"first_error=KeyDown/0x8000400");
  Replaced(zero, L"first_error=none", L"first_error=KeyDown/0x800040005");
  Replaced(zero, L"first_error=none", L"first_error=KeyDown/0x8000400g");
  Replaced(zero, L"first_error=none", L"first_error=KeyDown/0x8000400a");
  Replaced(Text(snapshot), L"saturated=1", L"saturated=0");
  puts("Host routing order, disposition, bounded snapshots, and parser schedules passed.");
  return 0;
}
