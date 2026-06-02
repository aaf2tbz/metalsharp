# Release Signing and Notarization

MetalSharp DMG releases must be signed with a Developer ID Application certificate and notarized before upload. Without that, macOS Gatekeeper can show the "Apple could not verify this app is free of malware" prompt and force users through Security & Privacy.

Release CI now fails before packaging if signing or notarization credentials are missing. Configure these GitHub Actions secrets:

- `MACOS_CERTIFICATE_P12`: base64-encoded Developer ID Application `.p12`
- `MACOS_CERTIFICATE_PASSWORD`: password for the `.p12`

Configure one notarization credential set:

- Apple ID credentials: `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`
- App Store Connect API key credentials: `APPLE_API_KEY_P8_BASE64`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER`

The release job imports the certificate into a temporary keychain, runs Electron Builder with Developer ID signing, notarizes through `app/build/notarize.cjs`, then validates the stapled app and DMG with `tools/dmg/verify-notarization.sh`.
