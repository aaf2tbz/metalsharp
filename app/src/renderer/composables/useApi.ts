/// <reference path="../api-types.ts" />
import type { ContractResponses, ContractRoute } from "../../shared/backend-contract";

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

// Prefer this for contract-covered routes. The generic api() remains available
// while the large existing renderer surface is migrated incrementally.
export async function contractApi<Route extends ContractRoute>(
  method: Route extends `${infer Method} ${string}` ? Method : never,
  url: Route extends `${string} ${infer Path}` ? Path | `${Path}?${string}` : never,
  body?: Record<string, unknown>,
  timeoutMs?: number,
): Promise<ContractResponses[Route] | null> {
  return api<ContractResponses[Route]>(method, url, body, timeoutMs);
}

export { getAPI };
