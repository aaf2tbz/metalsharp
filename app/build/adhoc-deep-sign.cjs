const fs = require("node:fs");
const path = require("node:path");
const { spawnSync } = require("node:child_process");

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    encoding: "utf8",
    stdio: options.capture ? "pipe" : "inherit",
  });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    const output = [result.stdout, result.stderr].filter(Boolean).join("\n");
    throw new Error(`${command} ${args.join(" ")} failed with status ${result.status}${output ? `\n${output}` : ""}`);
  }
  return result.stdout || "";
}

function shouldSkipAdhocSigning() {
  if (process.env.METALSHARP_SKIP_ADHOC_DEEP_SIGN === "1") {
    return "METALSHARP_SKIP_ADHOC_DEEP_SIGN=1";
  }

  const developerIdSigning =
    Boolean(process.env.CSC_KEYCHAIN) || Boolean(process.env.CSC_LINK) || Boolean(process.env.CSC_NAME);
  if (developerIdSigning && process.env.METALSHARP_UNSIGNED_DMG !== "1") {
    return "Developer ID signing is active";
  }

  return "";
}

function isMachO(filePath) {
  if (!fs.statSync(filePath).isFile()) {
    return false;
  }
  const output = run("file", ["-b", filePath], { capture: true });
  return output.includes("Mach-O");
}

function collectSignTargets(root) {
  const files = [];
  const bundles = [];
  const stack = [root];

  while (stack.length > 0) {
    const current = stack.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const fullPath = path.join(current, entry.name);
      if (entry.isSymbolicLink()) {
        continue;
      }
      if (entry.isDirectory()) {
        if (/\.(app|appex|framework|xpc)$/i.test(entry.name)) {
          bundles.push(fullPath);
        }
        stack.push(fullPath);
      } else if (entry.isFile() && isMachO(fullPath)) {
        files.push(fullPath);
      }
    }
  }

  bundles.sort((a, b) => b.split(path.sep).length - a.split(path.sep).length);
  files.sort((a, b) => b.split(path.sep).length - a.split(path.sep).length);
  return { files, bundles };
}

function signTarget(target, entitlementsPath = "") {
  const args = ["--force", "--sign", "-", "--timestamp=none"];
  if (entitlementsPath) {
    args.push("--entitlements", entitlementsPath);
  }
  args.push(target);
  run("codesign", args);
}

exports.default = async function adhocDeepSignMetalSharp(context) {
  if (context.electronPlatformName !== "darwin") {
    return;
  }

  const skipReason = shouldSkipAdhocSigning();
  if (skipReason) {
    console.log(`MetalSharp ad-hoc deep sign skipped: ${skipReason}.`);
    return;
  }

  const appName = context.packager.appInfo.productFilename;
  const appPath = path.join(context.appOutDir, `${appName}.app`);
  if (!fs.existsSync(appPath)) {
    throw new Error(`MetalSharp app bundle was not found for ad-hoc signing: ${appPath}`);
  }

  const entitlementsPath = path.join(__dirname, "entitlements.mac.plist");
  const { files, bundles } = collectSignTargets(appPath);
  console.log(`MetalSharp ad-hoc deep sign: ${files.length} Mach-O file(s), ${bundles.length} bundle(s).`);

  for (const file of files) {
    signTarget(file);
  }
  for (const bundle of bundles) {
    signTarget(bundle);
  }

  const finalArgs = ["--force", "--deep", "--strict", "--sign", "-", "--timestamp=none"];
  if (fs.existsSync(entitlementsPath)) {
    finalArgs.push("--entitlements", entitlementsPath);
  }
  finalArgs.push(appPath);
  run("codesign", finalArgs);

  run("codesign", ["--verify", "--deep", "--strict", "--verbose=2", appPath]);
  console.log("MetalSharp ad-hoc deep sign complete.");
};
