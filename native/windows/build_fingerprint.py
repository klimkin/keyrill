"""Generate the shared Windows artifact fingerprint from normalized source inputs."""

import argparse
import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def inputs():
    paths = {
        ROOT / name
        for name in (
            "Cargo.toml",
            "Cargo.lock",
            "rust-toolchain.toml",
            ".cargo/config.toml",
            "assets/model.bin",
        )
        if (ROOT / name).is_file()
    }
    for directory, suffixes in (
        ("include", {".h"}),
        ("crates", {".rs", ".toml"}),
        ("native/windows", {".rs", ".cpp", ".h", ".toml", ".def", ".ps1", ".py"}),
    ):
        paths.update(
            path
            for path in (ROOT / directory).rglob("*")
            if path.is_file() and path.suffix in suffixes
        )
    paths.update((ROOT / "native/windows").rglob("CMakeLists.txt"))
    return sorted(paths, key=lambda path: path.relative_to(ROOT).as_posix())


def fingerprint(paths):
    records = []
    for path in paths:
        content = path.read_bytes()
        if path.suffix != ".bin":
            content = content.replace(b"\r\n", b"\n")
        digest = hashlib.sha256(content).hexdigest()
        records.append(f"{path.relative_to(ROOT).as_posix()}:{digest}\n")
    return hashlib.sha256("".join(records).encode("utf-8")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--header", type=Path)
    parser.add_argument("--source", action="store_true")
    parser.add_argument("--feature", choices=("production", "test-api"), default="production")
    args = parser.parse_args()
    paths = inputs()
    if args.list:
        print("\n".join(path.relative_to(ROOT).as_posix() for path in paths))
        return
    source_digest = fingerprint(paths)
    digests = {}
    for feature in ("production", "test-api"):
        features = "" if feature == "production" else "test-api"
        domain = f"keyrill-windows-v1\nsource={source_digest}\nfeatures={features}\n"
        digests[feature] = hashlib.sha256(domain.encode("utf-8")).hexdigest()
    digest = source_digest if args.source else digests[args.feature]
    if args.header:
        args.header.parent.mkdir(parents=True, exist_ok=True)
        declarations = []
        for name, value in (
            ("source_build_digest", source_digest),
            ("production_build_digest", digests["production"]),
            ("test_build_digest", digests["test-api"]),
        ):
            words = ", ".join(f"0x{value[index:index + 8]}u" for index in range(0, 64, 8))
            declarations.append(f"inline constexpr std::array<uint32_t, 8> {name} = {{{words}}};\n")
        args.header.write_text(
            "#pragma once\n#include <array>\n#include <cstdint>\n"
            "namespace keyrill::probe {\n"
            + "".join(declarations)
            + "#ifdef KEYRILL_TEST_ARTIFACT\n"
            "inline constexpr auto build_digest = test_build_digest;\n"
            "#else\ninline constexpr auto build_digest = production_build_digest;\n#endif\n"
            "}\n",
            encoding="utf-8",
            newline="\n",
        )
    print(digest)


if __name__ == "__main__":
    main()
