# Private repository audit - October 2, 2026

## Scope

Version `1.0.24.0` is defined in `xbox_proxy/version.h`.
The current proxy sources and the six files in `revlimiter_haptics/mod`
were copied into this standalone repository. Runtime code, tests and
build/packaging scripts were preserved. Documentation was adapted for the
repository and subsequently translated into English. The original source
folders remain available outside this repository.

The internal `xbox_proxy` and `revlimiter_haptics` folder names preserve
the relative paths used by the source tools and tests. The Git directory
exists only at the root of this `revlimit_haptics` repository.

## Initial inventory and exclusions

The initial inventory covered 77 project files, 46 installed test-dependency
files and 4,670 files in the old Python virtual environment.
Sources, configuration, scripts, documentation, logs and build receipts
were inspected. Distribution archives were opened, including nested ZIP
files; executable and DLL strings were examined. Installed dependencies
and the virtual environment were inventoried and excluded as whole folders.

The following items were excluded from the copied source tree:

- `xbox_proxy/out/`: binaries, intermediate outputs, logs, build receipts,
  manifests, packages and temporary source copies.
- `xbox_proxy/.test-deps/`: installed test dependencies, native Python
  extensions (`.pyd`) and caches.
- Historical local reports `COMPTE_RENDU_CROISE_AUDIT_HAPTIQUES.md`,
  `RAPPORT_REVISION_HAPTIQUES.md` and `RAPPORT_POINTS_RESIDUELS_20260922.md`:
  optional working history. Their original filenames are retained here
  for identification.
- `revlimiter_haptics/.venv/` and Python caches: local environment files
  containing historical personal paths.
- `revlimiter_haptics/bridge/`, its tests and `BRIDGE_README.md`: the old
  SDL2 bridge uses NRH1, separate from the current version's BCH1 protocol.
- The old `revlimiter_haptics/README.md`: documentation for version 1.0.15.
- `revlimiter_haptics/revlimiter_haptics_mod.zip`: an older generated package.

The `.gitignore` also excludes common build outputs, DLL/EXE files, archives,
caches, logs, dumps, backups, temporary files, local configuration,
credentials, SSH keys and signing material.

## Binaries and distribution

| Item | Purpose | Distribution |
| --- | --- | --- |
| Proxy `XInput1_4.dll` | Required in the game's `Bin64` folder at runtime | Included in the release archive; excluded from Git source history |
| `native_tests.exe`, `load_test.exe` | Native regression tests and an optional smoke test | Development artifacts; excluded from Git and the install archive |
| `.obj`, `.lib`, `.exp`, `.res`, `.pdb`, `.ilk` | Compiler and linker intermediates | Excluded from Git and the install archive |
| Lua mod ZIP | Installs the telemetry mod paired with the proxy | Included in the release archive; excluded from Git source history |
| Native Python extensions (`.pyd`) | Installed test dependencies | Excluded from Git and the install archive |
| System XInput and Windows WinRT components | Operating-system dependencies | Provided by Windows |
| SDL2 DLLs and the old bridge | Previous NRH1 architecture | Outside the scope of this BCH1 version |

The ready-to-install release supplies all project-specific runtime binaries.
Its source tree requires no precompiled binaries. The CRT is statically linked
(`/MT`); Windows SDK libraries belong to the development toolchain.

## Sensitive-data checks

Checks covered private-key signatures, tokens, JWTs, credentials, connection
strings, certificates, SSH keys, network addresses, UNC paths, URLs and
personal absolute paths. No secrets or professional-infrastructure details
were detected in the selected sources.

The only network address used is `127.0.0.1`, loopback on UDP port 26780.
Strings such as `1.0.24.0` are version numbers. URLs in the documentation
and source tools point to Microsoft documentation and DigiCert's public
timestamp service.

`CODE_SIGN_CERT_THUMBPRINT` and `CODE_SIGN_TIMESTAMP_URL` are environment
variable names. No actual certificate thumbprint, certificate, private key
or credential is supplied. The signing script uses a local certificate
without exporting its private key.

The initial audit does not establish that future additions are safe.
Review files before committing and publishing them, including in a
private repository.

## Initial repository verification

- 23 text files were retained, with no precompiled binaries in Git.
- C++, Lua, JSON, tests and source tools matched the originals before
  Git line-ending normalization.
- Gitleaks 8.30.1 found no secrets. The scanner came from its official
  release; the archive's SHA-256 was checked before execution.
- Additional checks found no personal absolute paths, private IPv4
  addresses, UNC paths, embedded credentials, email addresses or
  suspicious high-entropy strings.
- Seven Lua tests and four applicable packaging guardrail tests passed.
  Three tests were skipped because their DLL fixtures were absent.
- Python, JSON and PowerShell syntax and C++ grammar were checked.
- MSVC and the Windows SDK were unavailable on the preparation machine;
  no new native compilation or in-game validation was performed.

Commits use the GitHub account name and its noreply email address to avoid
publishing a personal or professional email address.

## Release verification

The `v1.0.24.0` release is a private, unsigned Windows x64 prerelease.
The DLL comes from the validated build of September 25, 2026, which preceded
creation of this Git repository. Its version, exports, PE protections and
SHA-256 match the build receipt. All 17 code, test and source-tool files
match the initial repository commit after line-ending normalization;
differences are limited to documentation.

The install archive includes only the DLL, its matching six-file Lua mod
ZIP, installation instructions and checksums. Archive entries and nested
ZIP contents were verified. Gitleaks and additional string checks found
no secrets or professional-infrastructure details in the release content.

The English documentation update changes the installation text and
checksums in the outer archive. The DLL and inner Lua mod ZIP remain
byte-for-byte identical to the original release files.
