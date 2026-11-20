# Source and licensing

The Switch 2 port derives from sofianeelhor and contributors' PKForge at commit [c1d12b6](https://github.com/sofianeelhor/PKForge/tree/c1d12b6d1b0239e57ead0c1e1add1432d24f7e3a). The upstream project specifies GPLv3 or later. PKHeX and PKSM-Core provide shared save-format code. The native port adds the libounce controller interface, offline engine host, SD storage, build tools and save-format adaptations. The source snapshot of 20 November 2026 includes changes for local media, optional event tables, and binary notices.

The repository records the two managed dependencies as pinned Git submodules. The build version, commands, runtime source regeneration and SDK pin are in the [port guide](switch2/README.md). The tagged release includes the exact source tree and a corresponding-source download with the build inputs from both submodules. Source data tables retain their upstream references in their respective directories.

The NRO embeds the [third-party notices](switch2/romfs/THIRD-PARTY-NOTICES.txt). A byte-identical copy accompanies the binary download. The [visual asset credits](switch2/romfs/ui/ATTRIBUTION.md) list the fonts. Local media installed separately by a user is outside the release.

The NRO links [libounce at 39406be1](https://github.com/ouncebrew/libounce/tree/39406be1f25a2f1da3f7dfdaf7a42d2216cda4d0) from the [20261117 SDK](https://github.com/ouncebrew/ounce-devkit/releases/tag/devkit-20261117). The managed runtime uses [.NET 10.0.15](https://github.com/dotnet/runtime/tree/v10.0.15) and [WABT 1.0.39](https://github.com/WebAssembly/wabt/tree/1.0.39). The generated runtime C sources and the scripts used to build the native library are included under `switch2/managed/runtime` and `switch2/tools`.
