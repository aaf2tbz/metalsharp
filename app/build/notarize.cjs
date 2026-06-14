const path = require("node:path");
const { execFileSync } = require("node:child_process");
const { notarize } = require("@electron/notarize");

function hasPasswordCredentials() {
  return Boolean(process.env.APPLE_ID && process.env.APPLE_APP_SPECIFIC_PASSWORD && process.env.APPLE_TEAM_ID);
}

function hasApiKeyCredentials() {
  return Boolean(process.env.APPLE_API_KEY && process.env.APPLE_API_KEY_ID && process.env.APPLE_API_ISSUER);
}

function adhocSignApp(appPath) {
  const entitlements = path.join(__dirname, "entitlements.mac.plist");
  const args = ["--force", "--deep", "--sign", "-", "--options", "runtime"];
  if (entitlements) {
    args.push("--entitlements", entitlements);
  }
  args.push(appPath);

  execFileSync("/usr/bin/codesign", args, { stdio: "inherit" });
  execFileSync("/usr/bin/codesign", ["--verify", "--deep", "--strict", "--verbose=4", appPath], { stdio: "inherit" });
}

exports.default = async function notarizeMetalSharp(context) {
  if (context.electronPlatformName !== "darwin") {
    return;
  }

  const appName = context.packager.appInfo.productFilename;
  const appPath = path.join(context.appOutDir, `${appName}.app`);
  const requireNotarization = process.env.METALSHARP_REQUIRE_NOTARIZATION === "1";
  if (!hasPasswordCredentials() && !hasApiKeyCredentials()) {
    const message =
      "Apple notarization skipped: missing APPLE_ID/APPLE_APP_SPECIFIC_PASSWORD/APPLE_TEAM_ID or APPLE_API_KEY/APPLE_API_KEY_ID/APPLE_API_ISSUER.";
    if (requireNotarization) {
      throw new Error(message);
    }
    console.log(message);
    console.log("Apple Developer ID unavailable; applying ad-hoc app bundle signature for local Gatekeeper integrity.");
    adhocSignApp(appPath);
    return;
  }

  const baseOptions = {
    appBundleId: context.packager.appInfo.appId,
    appPath,
    tool: "notarytool",
  };

  if (hasApiKeyCredentials()) {
    await notarize({
      ...baseOptions,
      appleApiKey: process.env.APPLE_API_KEY,
      appleApiKeyId: process.env.APPLE_API_KEY_ID,
      appleApiIssuer: process.env.APPLE_API_ISSUER,
    });
    return;
  }

  await notarize({
    ...baseOptions,
    appleId: process.env.APPLE_ID,
    appleIdPassword: process.env.APPLE_APP_SPECIFIC_PASSWORD,
    teamId: process.env.APPLE_TEAM_ID,
  });
};
