# Matrix fork releases

`.github/workflows/release.yml` builds QLC+ 5 (QML UI) with Qt 6.11.2 for Ubuntu and Debian (AppImage), macOS (dmg) and Windows (installer), versioned `<upstream version>-matrix.<N>`.

- **Nightly**: runs daily at 03:17 UTC (skipped if the default branch has no new commits) and can be run by hand from the Actions tab (*Run workflow*, channel `nightly`). Replaces the rolling `nightly` pre-release; version is `<upstream>-matrix.nightly.<date>.<sha>`.
- **Stable**: push a tag `v<upstream version>-matrix.<N>`, e.g. `git tag v5.3.1-matrix.1 && git push origin v5.3.1-matrix.1`.
- Untick *publish* on a manual run to build only and keep the artifacts on the run page.
- Optional macOS signing secrets (same names as upstream): `MACOS_CERT_P12`, `MACOS_CERT_PASSWORD`, `MACOS_KEYCHAIN_PASSWORD`, `MACOS_CODESIGN_IDENTITY`, `MACOS_NOTARY_KEY_P8`, `MACOS_NOTARY_KEY_ID`, `MACOS_NOTARY_ISSUER`. Without them the dmg is unsigned.
- Requires *Settings > Actions > General > Workflow permissions* set to read and write.
- Fork-only: do not include these files in upstream PR branches.
