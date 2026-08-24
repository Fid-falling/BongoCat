# Microsoft Store packaging

The protected `store-package` job in `.github/workflows/ci.yml` creates a raw
`bongocat_<app-version>_x64.msix` artifact on pushes to the upstream `main`
branch of `vladelaina/BongoCat`. The artifact is intentionally unsigned:
Partner Center signs accepted MSIX packages with the Microsoft Store
certificate during submission.

## Store identity

- Package name: `vladelaina.bongocat`
- Publisher: `CN=5503A135-7FA4-466B-815C-DBE627F4065F`
- Publisher display name: `vladelaina`
- Package family name: `vladelaina.bongocat_hnew8t3b8e0t6`
- Store ID: `9P41MLSX72XW`

The package name and publisher are stored in `AppxManifest.xml.in`. The PFN
and package SID are derived by Windows and must not be used as signing
secrets. The Store package is x64; the separate Windows x86 workflow target
only publishes its desktop installer and portable archive.

MSIX versions must have a non-zero major component and a zero revision. The
script maps app version `0.1.1` to package version `1.1.1.0` by default; pass
`-PackageVersion` when advancing the Store version independently. The app
version remains in the filename so GitHub artifacts are easy to identify.

## Actions SDK setup

Live2D's licensed Cubism SDK is ignored by Git and cannot be checked into this
public repository. The protected job downloads the pinned Cubism 5-r.5 archive
and GLEW 2.2.0 archive from their official upstream URLs and verifies both
SHA-256 hashes before building. A repository owner may override either URL and
hash with these secrets when an approved mirror is required:

- `CUBISM_SDK_ARCHIVE_URL` and `CUBISM_SDK_ARCHIVE_SHA256`
- `CUBISM_GLEW_ARCHIVE_URL` and `CUBISM_GLEW_ARCHIVE_SHA256`

Running the protected workflow is subject to the Live2D Cubism SDK licenses;
the repository owner is responsible for satisfying their release terms.

The job verifies the digests and required files before configuring CMake with
`BONGO_CAT_REQUIRE_CUBISM=ON`; it never uploads the diagnostic backend.

## Local build and install check

On Windows with the Windows 10/11 SDK and the local Cubism SDK installed:

```powershell
cmake -S . -B build-cubism -G "Visual Studio 17 2022" -A x64 `
  -DBONGO_CAT_REQUIRE_CUBISM=ON -DBONGO_CAT_WARNINGS_AS_ERRORS=ON
cmake --build build-cubism --config Release --target bongo_cat --parallel 2

.\packaging\microsoft-store\build-store-package.ps1 `
  -ExecutablePath .\build-cubism\Release\BongoCat.exe `
  -Version 0.1.1
```

The script writes the unsigned Store submission package to
`output\microsoft-store\bongocat_0.1.1_x64.msix`. Upload that file directly to
Partner Center. It cannot be installed locally until Microsoft Store signs it.
Validate the exact Identity, PFN, version, architecture, payload, and unsigned
submission state with:

```powershell
.\packaging\microsoft-store\validate-store-package.ps1 `
  -PackagePath .\output\microsoft-store\bongocat_0.1.1_x64.msix `
  -ExpectedAppVersion 0.1.1
```

The protected Actions job runs this validation before uploading its artifact.

To test local deployment before submission, explicitly build a separate
self-signed package:

```powershell
.\packaging\microsoft-store\build-store-package.ps1 `
  -ExecutablePath .\build-cubism\Release\BongoCat.exe `
  -Version 0.1.1 -SignForLocalTesting

# Run these certificate/deployment commands from an elevated PowerShell.
Import-Certificate `
  .\output\microsoft-store\bongocat_0.1.1_x64-local-test.cer `
  -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
Add-AppxPackage `
  .\output\microsoft-store\bongocat_0.1.1_x64-local-test.msix
Get-AppxPackage -Name vladelaina.bongocat
```

The `-local-test` certificate and package are only for sideload testing. Do not
upload either one to Partner Center. The GitHub Actions job never passes
`-SignForLocalTesting` and always emits the unsigned Store submission package.
