/// <reference path="../api-types.ts" />

function getAPI(): MetalsharpAPI {
  return (window as unknown as { metalsharp: MetalsharpAPI }).metalsharp;
}

export async function api<T = unknown>(
  method: string,
  url: string,
  body?: Record<string, unknown>,
  timeoutMs?: number,
): Promise<T | null> {
  try {
    const res = await getAPI().request(method, url, body, timeoutMs);
    return (res.data ?? res) as T;
  } catch (e) {
    console.error(`API ${method} ${url}:`, e);
    return null;
  }
}

export function getRuntimeContracts() {
  return api<RuntimeContractsResponse>("GET", "/runtime/contracts");
}

export function getRuntimeContractReference() {
  return api<RuntimeContractReferenceResponse>("GET", "/runtime/contracts/reference");
}

export function getRuntimeManifest() {
  return api<RuntimeManifestResponse>("GET", "/runtime/manifest");
}

export function getRuntimeDiagnostics() {
  return api<RuntimeDiagnosticsResponse>("GET", "/runtime/diagnostics");
}

export function getLaunchValidation() {
  return api<LaunchValidationResponse>("GET", "/diagnostics/launch-validation");
}

export function getWine20RoadmapAudit() {
  return api<Wine20RoadmapAuditResponse>("GET", "/diagnostics/wine20-roadmap");
}

export function getLauncherProfiles() {
  return api<LauncherProfilesResponse>("GET", "/launcher/profiles");
}

export function getLauncherEvidenceInventory() {
  return api<LauncherEvidenceInventoryResponse>("GET", "/launcher/evidence");
}

export function getLauncherEvidence(body: { id?: string; family?: string }) {
  return api<LauncherEvidenceResponse>("POST", "/launcher/evidence", body);
}

export function getSourceAdapters() {
  return api<SourceAdaptersResponse>("GET", "/source-adapters");
}

export function previewSourcePrepare(body: { source: string; appId?: string | number; id?: string; productId?: string; route?: string; launchMethod?: string }) {
  return api<SourcePreparePreviewResponse>("POST", "/source-adapters/prepare", body);
}

export function dispatchSourceLaunch(body: { source: string; confirmed?: boolean; appId?: string | number; appid?: number; id?: string; productId?: string; route?: string; pipeline?: string; launchMethod?: string; engine?: string }) {
  return api<SourceLaunchDispatchResponse>("POST", "/source-adapters/launch", body);
}

export function getReceiptInventory() {
  return api<ReceiptInventoryResponse>("GET", "/diagnostics/receipts");
}

export { getAPI };
