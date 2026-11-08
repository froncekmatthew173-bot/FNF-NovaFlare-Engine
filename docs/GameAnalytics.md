# GameAnalytics build configuration

The REST client, event types and per-device identity storage are public in
`source/gameanalytics/`. Every platform compiles the same versioned implementation.
CI never downloads or restores analytics source code from Secrets.

## GitHub Actions

Open repository **Settings → Secrets and variables → Actions**, then add two
**Repository secrets**:

- `NOVA_GA_GAME_KEY`: GameAnalytics game key.
- `NOVA_GA_SECRET_KEY`: GameAnalytics signing secret.

All Windows, Linux, macOS, Android and iOS build jobs pass these Secrets to Haxe
as build environment variables. Existing `private-build` environment protections
are preserved. Environment secrets with the same names take precedence over
repository secrets.

Official builds require both keys; a missing key fails the build with a message
containing only the missing variable names. Fork builds without credentials keep
analytics disabled. Reusable release workflows already pass `secrets: inherit`.

The old `NOVA_GA_IMPL_B64`, `NOVA_GA_TYPES_B64` and `NOVA_GA_CONFIG_B64` Secrets
are no longer read and may be removed after migration.

## Local builds

GitHub Secrets exist only on Actions runners; local builds need their own
environment variables with the same names. In PowerShell, set both variables
before running Lime:

```powershell
$env:NOVA_GA_GAME_KEY = 'your-game-key'
$env:NOVA_GA_SECRET_KEY = 'your-signing-secret'
lime build android -release
```

Alternatively set Windows user environment variables and restart VS Code or the
terminal so new processes inherit them. Never commit real keys to source files.

`NOVA_GA_BUILD_VERSION` optionally overrides the analytics build label.
`NOVA_GA_REQUIRED=1` requires keys for a local build as well.
With neither key set, local builds succeed with analytics disabled. Setting only
one key is an error. Debug logging and sandbox defaults retain their previous
behavior.

The compiler embeds credentials in the executable. Secrets protect source
control and build configuration; they do not make embedded client keys
impossible to extract. Haxe `private` is a code access modifier, not encryption.
