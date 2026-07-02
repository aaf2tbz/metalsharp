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

export function getRuntimeManifest() {
  return api<RuntimeManifestResponse>("GET", "/runtime/manifest");
}

export function getRuntimeDiagnostics() {
  return api<RuntimeDiagnosticsResponse>("GET", "/runtime/diagnostics");
}

export function getLaunchValidation() {
  return api<LaunchValidationResponse>("GET", "/diagnostics/launch-validation");
}

export function getSourceAdapters() {
  return api<SourceAdaptersResponse>("GET", "/source-adapters");
}

export function previewSourcePrepare(body: { source: string; appId?: string | number; id?: string; productId?: string; route?: string; launchMethod?: string }) {
  return api<SourcePreparePreviewResponse>("POST", "/source-adapters/prepare", body);
}

export function getReceiptInventory() {
  return api<ReceiptInventoryResponse>("GET", "/diagnostics/receipts");
}

export { getAPI };
