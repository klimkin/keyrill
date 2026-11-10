#include "host_inspect_options.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace keyrill::inspection;
namespace {
void Require(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}
bool Parse(std::initializer_list<const wchar_t *> arguments, Options &options) {
  return ParseOptions(static_cast<int>(arguments.size()), arguments.begin(), options);
}
} // namespace
int main() {
  const auto *digest = L"0123456789abcdefABCDEF019876543200000000ffffffff8000000000000001";
  Options options;
  Require(Parse({L"inspect", L"build"}, options) && options.command == Command::Build,
          "existing build command accepted");
  for (const auto *command : {L"host", L"service", L"public-empty"})
    Require(Parse({L"inspect", command, L"123"}, options) && options.pid == 123 &&
                options.seconds == 0 && !options.expected_build,
            "existing default command retains implicit observer build expectation");
  Require(Parse({L"inspect", L"service", L"123", L"--arm-seconds", L"3600"}, options) &&
              options.seconds == 3600 && !options.expected_build,
          "existing bounded arm option accepted");
  Require(Parse({L"inspect", L"service", L"123", L"--expect-server-build", digest}, options) &&
              options.expected_build == BuildDigest{0x01234567, 0x89abcdef, 0xabcdef01, 0x98765432,
                                                    0, 0xffffffff, 0x80000000, 1},
          "exact mixed-case digest parses to canonical word order");
  Require(Parse({L"inspect", L"service", L"123", L"--arm-seconds", L"1", L"--expect-server-build",
                 digest},
                options) &&
              options.seconds == 1 && options.expected_build,
          "explicit reviewed server build and arming accepted together");
  Require(Parse({L"inspect", L"service", L"123", L"--expect-server-build", digest, L"--arm-seconds",
                 L"1"},
                options),
          "service options accepted in either order");
  for (const auto *invalid :
       {L"", L"0", L"-1", L"+1", L" 1", L"4294967295", L"4294967296", L"999999999999999999999"})
    Require(!Parse({L"inspect", L"service", invalid}, options), "invalid PID refused");
  for (const auto *invalid : {L"0", L"3601", L"-1", L"1x"})
    Require(!Parse({L"inspect", L"service", L"123", L"--arm-seconds", invalid}, options),
            "invalid arm duration refused");
  for (const auto *invalid :
       {L"", L"0", L"0123456789abcdefABCDEF019876543200000000ffffffff800000000000000",
        L"0123456789abcdefABCDEF019876543200000000ffffffff80000000000000011",
        L"g123456789abcdefABCDEF019876543200000000ffffffff8000000000000001",
        L" 123456789abcdefABCDEF019876543200000000ffffffff8000000000000001"})
    Require(!Parse({L"inspect", L"service", L"123", L"--expect-server-build", invalid}, options),
            "nonexact or nonhex digest refused");
  for (const auto *command : {L"host", L"public-empty", L"build"})
    Require(!Parse({L"inspect", command, L"123", L"--expect-server-build", digest}, options),
            "explicit server digest is service-only");
  Require(!Parse({L"inspect", L"service", L"123", L"--expect-server-build"}, options),
          "missing option value refused");
  Require(!Parse({L"inspect", L"service", L"123", L"--expect-server-build", digest,
                  L"--expect-server-build", digest},
                 options),
          "duplicate expected digest refused");
  Require(!Parse({L"inspect", L"service", L"123", L"--arm-seconds", L"1", L"--arm-seconds", L"2"},
                 options),
          "duplicate arm duration refused");
  Require(!Parse({L"inspect", L"service", L"123", L"--ignore-build", digest}, options),
          "unknown option refused");
  puts("host inspector argument schedules passed");
}
