#!/usr/bin/env node

import fs from "node:fs";
import http from "node:http";
import path from "node:path";

const controls = [
  { label: "nidhogg2", appid: 535520, expectedLaunchMethod: "m9" },
  { label: "schedule-i", appid: 3164500, expectedLaunchMethod: "m11" },
  { label: "subnautica-bz", appid: 848450, expectedLaunchMethod: "m11" },
];

const wineRouteIds = new Set([
  "steam",
  "wine_steam",
  "m9",
  "m10",
  "m11",
  "m12",
  "m32",
  "dx9",
  "dx10",
  "dx11",
  "dx12",
  "d3d9",
  "d3d10",
  "d3d11",
  "d3d12",
  "d3d9_metal",
  "dxmt_metal",
  "dxmt_metal12",
  "wined3d_32",
]);

function usage() {
  console.error(`Usage:
  node scripts/audit-electron-launch-routes.mjs --base-url http://127.0.0.1:9274 --out-dir /tmp/route-audit
  node scripts/audit-electron-launch-routes.mjs --library-json /tmp/steam-library.json --out-dir /tmp/route-audit

Audits the Electron LibraryView launch routing decision for the Wine 11.9
control games. This is non-live: it does not launch games.`);
}

function argValue(args, name) {
  const index = args.indexOf(name);
  return index >= 0 ? args[index + 1] : undefined;
}

function getJson(url) {
  return new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      const chunks = [];
      res.on("data", (chunk) => chunks.push(chunk));
      res.on("end", () => {
        const text = Buffer.concat(chunks).toString("utf8");
        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(new Error(`${url} returned HTTP ${res.statusCode}: ${text}`));
          return;
        }
        try {
          resolve(JSON.parse(text));
        } catch (error) {
          reject(new Error(`${url} did not return JSON: ${error.message}`));
        }
      });
    });
    req.on("error", reject);
    req.setTimeout(5000, () => {
      req.destroy(new Error(`${url} timed out`));
    });
  });
}

function isMacSteamLaunch(launchMethod) {
  return launchMethod === "mac_steam" || launchMethod === "macos_steam" || launchMethod === "native_steam";
}

function isWineSteamRouteId(launchMethod) {
  return wineRouteIds.has(String(launchMethod ?? "").toLowerCase());
}

function recommendedLaunchMethod(game) {
  return game.launch_method ?? game.available_pipelines?.find((pipeline) => pipeline.recommended)?.id;
}

function isWineSteamRouteLaunch(game, launchMethod) {
  const method = String(launchMethod).toLowerCase();
  if (method === "auto") {
    const recommended = recommendedLaunchMethod(game);
    if (!recommended) return true;
    if (isMacSteamLaunch(recommended)) return false;
    return isWineSteamRouteId(recommended);
  }
  return isWineSteamRouteId(method);
}

function endpointFor(game, launchMethod) {
  return isWineSteamRouteLaunch(game, launchMethod) ? "/steam/launch-game" : "/game/launch-auto";
}

async function main() {
  const args = process.argv.slice(2);
  const baseUrl = argValue(args, "--base-url");
  const libraryJson = argValue(args, "--library-json");
  const outDir = argValue(args, "--out-dir") ?? `/tmp/metalsharp-electron-route-audit-${Date.now()}`;

  if ((!baseUrl && !libraryJson) || args.includes("-h") || args.includes("--help")) {
    usage();
    process.exit(args.includes("-h") || args.includes("--help") ? 0 : 1);
  }

  fs.mkdirSync(outDir, { recursive: true });

  const library = libraryJson
    ? JSON.parse(fs.readFileSync(libraryJson, "utf8"))
    : await getJson(`${baseUrl.replace(/\/$/, "")}/steam/library`);

  fs.writeFileSync(path.join(outDir, "steam-library.json"), `${JSON.stringify(library, null, 2)}\n`);

  const games = Array.isArray(library.games) ? library.games : [];
  const rows = [];
  let failures = 0;

  for (const control of controls) {
    const game = games.find((candidate) => Number(candidate.appid) === control.appid);
    const backendPipelines = baseUrl
      ? await getJson(`${baseUrl.replace(/\/$/, "")}/mtsp/pipelines?appid=${control.appid}`).catch((error) => ({
          ok: false,
          error: error.message,
        }))
      : null;

    if (!game) {
      failures += 1;
      rows.push({
        ...control,
        ok: false,
        error: "control game missing from /steam/library",
      });
      continue;
    }

    const autoEndpoint = endpointFor(game, "auto");
    const explicitEndpoint = endpointFor(game, control.expectedLaunchMethod);
    const recommended = recommendedLaunchMethod(game) ?? null;
    const ok = autoEndpoint === "/steam/launch-game" && explicitEndpoint === "/steam/launch-game";
    if (!ok) failures += 1;

    rows.push({
      ...control,
      ok,
      name: game.name,
      installed: game.installed,
      gameLaunchMethod: game.launch_method ?? null,
      recommendedFromLibrary: recommended,
      backendRecommended: backendPipelines?.recommended ?? null,
      autoEndpoint,
      explicitEndpoint,
      note:
        autoEndpoint === "/steam/launch-game"
          ? "Electron auto route stays on Wine Steam attach path"
          : "Electron auto route falls back to /game/launch-auto",
    });
  }

  const result = {
    ok: failures === 0,
    failures,
    controls: rows,
  };
  fs.writeFileSync(path.join(outDir, "electron-launch-route-audit.json"), `${JSON.stringify(result, null, 2)}\n`);

  const lines = [
    "# MetalSharp Electron Launch Route Audit",
    "",
    `- failures: ${failures}`,
    `- gate: ${failures === 0 ? "pass" : "fail"}`,
    "",
    "| Game | AppID | Expected Method | Library Recommended | Backend Recommended | Auto Endpoint | Explicit Endpoint | Gate |",
    "| --- | --- | --- | --- | --- | --- | --- | --- |",
  ];
  for (const row of rows) {
    lines.push(
      `| ${row.name ?? row.label} | ${row.appid} | ${row.expectedLaunchMethod} | ${row.recommendedFromLibrary ?? "none"} | ${row.backendRecommended ?? "not-probed"} | ${row.autoEndpoint ?? "missing"} | ${row.explicitEndpoint ?? "missing"} | ${row.ok ? "pass" : "fail"} |`,
    );
  }
  lines.push("");
  lines.push("This audit mirrors `LibraryView.vue` routing: `auto` uses `/steam/launch-game` unless the library metadata explicitly selects macOS/native Steam.");
  fs.writeFileSync(path.join(outDir, "electron-launch-route-audit.md"), `${lines.join("\n")}\n`);

  console.log(`electron launch route audit written: ${outDir}`);
  if (failures > 0) process.exit(2);
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
