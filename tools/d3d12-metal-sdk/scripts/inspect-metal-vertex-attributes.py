#!/usr/bin/env python3
"""Inspect Metal vertex function attributes for captured render PSO manifests.

This is scratch-corpus tooling only. Captured manifests contain D3D12 input
layout metadata; Metal's final validator uses `MTLFunction.vertexAttributes`.
This script loads each vertex function from the selected corpus root, records
those attributes, and compares them to the manifest so runtime fixes can be
based on observed Metal reflection instead of guesses.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any


HARNESS_SOURCE = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static NSString *stringValue(NSDictionary *dict, NSString *key, NSString *fallback) {
  id value = dict[key];
  return [value isKindOfClass:[NSString class]] ? value : fallback;
}

static NSDictionary *loadJSON(NSString *path) {
  NSData *data = [NSData dataWithContentsOfFile:path];
  if (!data) return nil;
  return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

static NSDictionary *fail(NSString *status, NSString *message) {
  return @{ @"ok": @NO, @"status": status ?: @"failed", @"error": message ?: @"unknown" };
}

static NSDictionary *ok(NSArray *attributes) {
  return @{ @"ok": @YES, @"status": @"ok", @"attributes": attributes ?: @[] };
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc < 2) return 2;
    NSDictionary *input = loadJSON([NSString stringWithUTF8String:argv[1]]);
    NSArray *items = input[@"items"];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSMutableArray *results = [NSMutableArray array];
    for (NSDictionary *item in items) {
      NSMutableDictionary *row = [item mutableCopy];
      NSString *metallib = stringValue(item, @"metallib", @"");
      NSString *functionName = stringValue(item, @"function", @"vs_main");
      NSData *data = [NSData dataWithContentsOfFile:metallib];
      if (!data) {
        [row addEntriesFromDictionary:fail(@"metallib_not_readable", metallib)];
        [results addObject:row];
        continue;
      }
      dispatch_data_t dispatchData = dispatch_data_create([data bytes], [data length], dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
      NSError *error = nil;
      id<MTLLibrary> library = [device newLibraryWithData:dispatchData error:&error];
      if (!library) {
        [row addEntriesFromDictionary:fail(@"library_failed", error ? [error localizedDescription] : @"newLibraryWithData failed")];
        [results addObject:row];
        continue;
      }
      id<MTLFunction> fn = [library newFunctionWithName:functionName];
      NSString *reflectionPath = stringValue(item, @"reflection", @"");
      NSDictionary *reflection = loadJSON(reflectionPath);
      NSString *entryPoint = [reflection isKindOfClass:[NSDictionary class]] ? stringValue(reflection, @"EntryPoint", @"") : @"";
      if (!fn && [entryPoint length] > 0) fn = [library newFunctionWithName:entryPoint];
      if (!fn) fn = [library newFunctionWithName:@"vs_main"];
      if (!fn) {
        [row addEntriesFromDictionary:fail(@"function_failed", [NSString stringWithFormat:@"%@ entry=%@", functionName, entryPoint])];
        [results addObject:row];
        continue;
      }
      NSMutableArray *attrs = [NSMutableArray array];
      for (MTLVertexAttribute *attr in [fn vertexAttributes]) {
        [attrs addObject:@{
          @"name": [attr name] ?: @"",
          @"attribute_index": @([attr attributeIndex]),
          @"attribute_type": @((NSUInteger)[attr attributeType]),
          @"active": @([attr isActive]),
          @"patch_data": @([attr isPatchData]),
          @"patch_control_point_data": @([attr isPatchControlPointData]),
        }];
      }
      [row addEntriesFromDictionary:ok(attrs)];
      [results addObject:row];
    }
    NSDictionary *output = @{ @"schema": @"metalsharp.vertex-attributes.raw.v1", @"results": results };
    NSData *json = [NSJSONSerialization dataWithJSONObject:output options:NSJSONWritingPrettyPrinted error:nil];
    fwrite([json bytes], 1, [json length], stdout);
    fputc('\n', stdout);
    return 0;
  }
}
'''


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text())
    except Exception:
        return None


def compile_harness(build_dir: Path) -> Path:
    source = build_dir / "inspect_vertex_attributes.mm"
    binary = build_dir / "inspect_vertex_attributes"
    source.write_text(HARNESS_SOURCE)
    subprocess.check_call([
        "clang++",
        "-std=c++17",
        "-fobjc-arc",
        "-framework",
        "Foundation",
        "-framework",
        "Metal",
        str(source),
        "-o",
        str(binary),
    ])
    return binary


def manifest_items(corpus: Path, limit: int) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    items: list[dict[str, Any]] = []
    manifest_index: dict[str, dict[str, Any]] = {}
    for manifest_path in sorted(corpus.glob("pso-render-*.json")):
        data = load_json(manifest_path)
        if not data:
            continue
        for pipeline in data.get("pipelines") or []:
            if not isinstance(pipeline, dict):
                continue
            d3d12 = pipeline.get("d3d12") or {}
            vertex = pipeline.get("vertex") or {}
            vs_hash = str(d3d12.get("vs_hash") or vertex.get("hash") or "")
            if not vs_hash:
                continue
            name = str(pipeline.get("name") or manifest_path.stem)
            item = {
                "name": name,
                "manifest": str(manifest_path),
                "vs_hash": vs_hash,
                "metallib": str(corpus / f"{vs_hash}.metallib"),
                "reflection": str(corpus / f"{vs_hash}.json"),
                "function": str(vertex.get("function") or "vs_main"),
            }
            items.append(item)
            manifest_index[name] = pipeline | {"manifest_path": str(manifest_path)}
            if limit and len(items) >= limit:
                return items, manifest_index
    return items, manifest_index


def load_reflection_vertex_inputs(path: Path) -> list[dict[str, Any]]:
    data = load_json(path)
    if not data:
        return []
    state = data.get("state") if isinstance(data.get("state"), dict) else {}
    inputs = state.get("vertex_inputs") if isinstance(state, dict) else []
    return inputs if isinstance(inputs, list) else []


def summarize(raw: dict[str, Any], manifest_index: dict[str, dict[str, Any]], corpus: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    counts = Counter()
    attr_type_counts = Counter()
    missing_reflection = 0
    for row in raw.get("results") or []:
        name = row.get("name")
        pipeline = manifest_index.get(name, {})
        d3d12 = pipeline.get("d3d12") or {}
        input_layout = pipeline.get("input_layout") or {}
        manifest_elements = input_layout.get("elements") if isinstance(input_layout, dict) else []
        reflection_inputs = load_reflection_vertex_inputs(corpus / f"{row.get('vs_hash')}.json")
        attributes = row.get("attributes") if isinstance(row.get("attributes"), list) else []
        if row.get("ok"):
            if attributes:
                bucket = "has_metal_vertex_attributes"
            else:
                bucket = "no_metal_vertex_attributes"
        else:
            bucket = str(row.get("status") or "failed")
        counts[bucket] += 1
        if not reflection_inputs:
            missing_reflection += 1
        for attr in attributes:
            attr_type_counts[str(attr.get("attribute_type"))] += 1
        rows.append({
            "name": name,
            "bucket": bucket,
            "vs_hash": row.get("vs_hash"),
            "status": row.get("status"),
            "error": row.get("error"),
            "manifest": row.get("manifest"),
            "d3d12_input_elements": d3d12.get("input_elements"),
            "manifest_input_element_count": len(manifest_elements) if isinstance(manifest_elements, list) else 0,
            "reflection_vertex_input_count": len(reflection_inputs),
            "metal_vertex_attribute_count": len(attributes),
            "metal_vertex_attributes": attributes,
            "reflection_vertex_inputs": reflection_inputs[:32],
        })
    return {
        "schema": "metalsharp.d3d12-metal.vertex-attribute-inspection.v1",
        "corpus": str(corpus),
        "pipeline_count": len(rows),
        "bucket_counts": dict(sorted(counts.items())),
        "attribute_type_counts": dict(sorted(attr_type_counts.items())),
        "missing_reflection_count": missing_reflection,
        "rows": rows,
    }


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = [
        "# Metal Vertex Attribute Inspection",
        "",
        f"- Corpus: `{summary['corpus']}`",
        f"- Pipelines: {summary['pipeline_count']}",
        "",
        "## Bucket Counts",
        "",
    ]
    for bucket, count in summary["bucket_counts"].items():
        lines.append(f"- `{bucket}`: {count}")
    lines += ["", "## Attribute Type Counts", ""]
    for kind, count in summary["attribute_type_counts"].items():
        lines.append(f"- `{kind}`: {count}")
    lines += ["", "## First Rows With Attributes", ""]
    for row in [r for r in summary["rows"] if r["metal_vertex_attribute_count"]][:20]:
        attrs = ", ".join(f"{a.get('name')}[{a.get('attribute_index')}] type={a.get('attribute_type')}" for a in row["metal_vertex_attributes"][:12])
        lines.append(f"- `{row['name']}` vs=`{row['vs_hash']}` d3d12_inputs={row['d3d12_input_elements']} reflection_inputs={row['reflection_vertex_input_count']} attrs={attrs}")
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--results-dir", type=Path, default=Path("/tmp/metalsharp-m12-vertex-attrs"))
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument("--limit", type=int, default=0)
    args = parser.parse_args()

    args.results_dir.mkdir(parents=True, exist_ok=True)
    items, manifest_index = manifest_items(args.corpus, args.limit)
    with tempfile.TemporaryDirectory(prefix="metalsharp-vertex-attrs-") as tmp:
        build = Path(tmp)
        input_path = build / "input.json"
        input_path.write_text(json.dumps({"items": items}, indent=2) + "\n")
        binary = compile_harness(build)
        completed = subprocess.run([str(binary), str(input_path)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if completed.returncode != 0:
            raise SystemExit(completed.stderr or completed.returncode)
        raw = json.loads(completed.stdout)
    summary = summarize(raw, manifest_index, args.corpus)
    out = args.results_dir / f"metal-vertex-attributes-{args.profile}.json"
    out.write_text(json.dumps(summary, indent=2) + "\n")
    write_markdown(summary, out.with_suffix(".md"))
    print(out)
    print(out.with_suffix(".md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
