# Release process

CI builds and tests the project on Windows x64, Windows x86 and Linux for the
portable protocol core.

Development builds run on every push to `main` from
`.github/workflows/dev.yml`. They upload workflow artifacts named
`g25-w11-driver-dev-<sha>` without creating a GitHub release.

Nightly builds run from `.github/workflows/nightly.yml` every night and can also
be started manually from GitHub Actions. The workflow publishes a moving
`nightly` prerelease containing:

- `g25-w11-driver-nightly-<run>.zip`
- `g25-w11-driver-nightly-<run>-setup.exe`

Versioned releases are created by pushing a tag named `v*`, for example:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

The release workflow builds both architectures, generates `dist/CHANGELOG.md`,
packages the portable zip and compiles the Windows installer with Inno Setup.

For a local package build using existing binaries:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Package-Release.ps1 `
  -Version local `
  -Build64 build\portable-release `
  -Build32 build\portable-release-x86
```

If Inno Setup 6 is installed locally, the installer can be built from the
generated package:

```powershell
$env:G25_VERSION = 'local'
$env:G25_PACKAGE_DIR = (Resolve-Path dist\g25-w11-driver-local).Path
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\g25-w11-driver.iss
```
