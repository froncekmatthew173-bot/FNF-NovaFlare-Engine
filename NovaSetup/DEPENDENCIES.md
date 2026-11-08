# GitHub dependency mirrors

Run these commands from the repository root after installing Haxe, Neko and Git:

```sh
haxelib git hmm https://github.com/NovaFlare-Engine-haxelib/hmm-3.1.0 70aca779a9d374a6e6b83c18715a3991a147ca37 --skip-dependencies
haxelib run hmm install
```

Use this HMM mirror for installs and reinstalls. It includes the precompiled Neko runner and passes `--skip-dependencies` to Git installs. The manifest explicitly lists indirect dependencies, including `hxjsonast`, `tink_core` and Lime's build tool `hxp`. This avoids implicit dependency downloads from lib.haxe.org, including fixed-version dependencies.

The libraries previously downloaded from haxelib are imported from the installed distributions, with their original versions, package files, licenses and attribution preserved. The HMM runner is rebuilt from the included source with the Git installation change described in its `MIRROR.md`. All dependencies are pinned to commits rather than moving branches. Git checkouts are pinned to the versions selected for this release. Lime retains commit `e4adfed089eacf9e6eb1bc3ed306eb2e68336256` from the last successful release; the newer native binaries at `e988b54` fail during iOS launch-image generation.

The table below lists the distribution mirrors. The active `haxeui-flixel` dependency uses the existing [NovaFlare fork](https://github.com/NovaFlare-Engine-haxelib/haxeui-flixel), which includes the local `include.xml` fix; its exact commit is recorded in `hmm.json`.

| Library | Version | Mirror |
| --- | --- | --- |
| flixel-tools | 1.5.1 | [flixel-tools-1.5.1](https://github.com/NovaFlare-Engine-haxelib/flixel-tools-1.5.1) |
| haxeui-flixel | 1.7.0 | [haxeui-flixel-1.7.0](https://github.com/NovaFlare-Engine-haxelib/haxeui-flixel-1.7.0) |
| crypto | 1.0.4 | [crypto-1.0.4](https://github.com/NovaFlare-Engine-haxelib/crypto-1.0.4) |
| markdown | 1.1.3 | [markdown-1.1.3](https://github.com/NovaFlare-Engine-haxelib/markdown-1.1.3) |
| hxcpp-debug-server | 1.2.4 | [hxcpp-debug-server-1.2.4](https://github.com/NovaFlare-Engine-haxelib/hxcpp-debug-server-1.2.4) |
| tjson | 1.4.0 | [tjson-1.4.0](https://github.com/NovaFlare-Engine-haxelib/tjson-1.4.0) |
| hxdiscord_rpc | 1.3.0 | [hxdiscord_rpc-1.3.0](https://github.com/NovaFlare-Engine-haxelib/hxdiscord_rpc-1.3.0) |
| format | 3.5.0 | [format-3.5.0](https://github.com/NovaFlare-Engine-haxelib/format-3.5.0) |
| jsonpatch | 1.1.0 | [jsonpatch-1.1.0](https://github.com/NovaFlare-Engine-haxelib/jsonpatch-1.1.0) |
| jsonpath | 1.1.0 | [jsonpath-1.1.0](https://github.com/NovaFlare-Engine-haxelib/jsonpath-1.1.0) |
| hxjsonast | 1.1.0 | [hxjsonast-1.1.0](https://github.com/NovaFlare-Engine-haxelib/hxjsonast-1.1.0) |
| tink_core | 1.26.0 | [tink_core-1.26.0](https://github.com/NovaFlare-Engine-haxelib/tink_core-1.26.0) |
| hxp | 1.3.1 | [hxp-1.3.1](https://github.com/NovaFlare-Engine-haxelib/hxp-1.3.1) |
| hmm | 3.1.0 | [hmm-3.1.0](https://github.com/NovaFlare-Engine-haxelib/hmm-3.1.0) |

To update a mirrored package, publish the new version under the organization and update its commit in `hmm.json`. For HMM, update the bootstrap commit in the setup script and build workflows as well.
