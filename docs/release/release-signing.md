# Release Signing and Notarization

MetalSharp DMG releases must be signed with a Developer ID Application certificate and notarized before upload. Without that, macOS Gatekeeper can show the "Apple could not verify this app is free of malware" prompt and force users through Security & Privacy.

Configure these GitHub Actions secrets to produce a signed and notarized DMG:

- `MACOS_CERTIFICATE_P12`: base64-encoded Developer ID Application `.p12`
- `MACOS_CERTIFICATE_PASSWORD`: password for the `.p12`

Configure one notarization credential set:

- Apple ID credentials: `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`
- App Store Connect API key credentials: `APPLE_API_KEY_P8_BASE64`, `APPLE_API_KEY_ID`, `APPLE_API_ISSUER`

The release job imports the certificate into a temporary keychain, runs Electron Builder with Developer ID signing, notarizes through `app/build/notarize.cjs`, then validates the stapled app and DMG with `tools/dmg/verify-notarization.sh`. The notarization verifier requires a Developer ID Application identity on the app, a stapled ticket, accepted Gatekeeper assessment, and a structurally valid DMG image before upload.

If the Apple secrets are not configured yet, Release CI falls back to an unsigned/ad-hoc DMG instead of skipping the release entirely. The fallback disables Electron Builder certificate discovery, packages the DMG, verifies the embedded runtime assets, and uploads a `DMG-SIGNING.txt` marker beside the DMG. This keeps release artifacts available during credential setup, but the unsigned DMG can still trigger Gatekeeper warnings until the Developer ID and notarization secrets are configured.
