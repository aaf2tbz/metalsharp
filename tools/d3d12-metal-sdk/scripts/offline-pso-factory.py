#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


HARNESS_SOURCE = r'''
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

static NSString *stringValue(NSDictionary *dict, NSString *key, NSString *fallback) {
  id value = dict[key];
  return [value isKindOfClass:[NSString class]] ? value : fallback;
}

static NSUInteger uintValue(NSDictionary *dict, NSString *key, NSUInteger fallback) {
  id value = dict[key];
  return [value respondsToSelector:@selector(unsignedIntegerValue)] ? [value unsignedIntegerValue] : fallback;
}

static MTLPixelFormat pixelFormat(NSString *name) {
  if (!name || [name length] == 0 || [name isEqualToString:@"invalid"] || [name isEqualToString:@"unknown"]) return MTLPixelFormatInvalid;
  NSString *n = [name lowercaseString];
  if ([n isEqualToString:@"rgba8unorm"] || [n isEqualToString:@"r8g8b8a8_unorm"]) return MTLPixelFormatRGBA8Unorm;
  if ([n isEqualToString:@"rgba8unorm_srgb"] || [n isEqualToString:@"r8g8b8a8_unorm_srgb"]) return MTLPixelFormatRGBA8Unorm_sRGB;
  if ([n isEqualToString:@"bgra8unorm"] || [n isEqualToString:@"b8g8r8a8_unorm"]) return MTLPixelFormatBGRA8Unorm;
  if ([n isEqualToString:@"bgra8unorm_srgb"] || [n isEqualToString:@"b8g8r8a8_unorm_srgb"]) return MTLPixelFormatBGRA8Unorm_sRGB;
  if ([n isEqualToString:@"r8unorm"] || [n isEqualToString:@"r8_unorm"]) return MTLPixelFormatR8Unorm;
  if ([n isEqualToString:@"rg8unorm"] || [n isEqualToString:@"r8g8_unorm"]) return MTLPixelFormatRG8Unorm;
  if ([n isEqualToString:@"r16float"] || [n isEqualToString:@"r16_float"]) return MTLPixelFormatR16Float;
  if ([n isEqualToString:@"r16unorm"] || [n isEqualToString:@"r16_unorm"]) return MTLPixelFormatR16Unorm;
  if ([n isEqualToString:@"rg16float"] || [n isEqualToString:@"r16g16_float"]) return MTLPixelFormatRG16Float;
  if ([n isEqualToString:@"rg16unorm"] || [n isEqualToString:@"r16g16_unorm"]) return MTLPixelFormatRG16Unorm;
  if ([n isEqualToString:@"rgba16float"] || [n isEqualToString:@"r16g16b16a16_float"]) return MTLPixelFormatRGBA16Float;
  if ([n isEqualToString:@"rgba16unorm"] || [n isEqualToString:@"r16g16b16a16_unorm"]) return MTLPixelFormatRGBA16Unorm;
  if ([n isEqualToString:@"rg11b10float"] || [n isEqualToString:@"r11g11b10_float"]) return MTLPixelFormatRG11B10Float;
  if ([n isEqualToString:@"rgb10a2unorm"] || [n isEqualToString:@"r10g10b10a2_unorm"]) return MTLPixelFormatRGB10A2Unorm;
  if ([n isEqualToString:@"rgba32float"] || [n isEqualToString:@"r32g32b32a32_float"]) return MTLPixelFormatRGBA32Float;
  if ([n isEqualToString:@"r32float"] || [n isEqualToString:@"r32_float"]) return MTLPixelFormatR32Float;
  if ([n isEqualToString:@"r32uint"] || [n isEqualToString:@"r32_uint"]) return MTLPixelFormatR32Uint;
  if ([n isEqualToString:@"depth32float"] || [n isEqualToString:@"d32_float"]) return MTLPixelFormatDepth32Float;
  if ([n isEqualToString:@"depth24unorm_stencil8"] || [n isEqualToString:@"d24_unorm_s8_uint"]) return MTLPixelFormatDepth24Unorm_Stencil8;
  if ([n isEqualToString:@"depth32float_stencil8"] || [n isEqualToString:@"d32_float_s8x24_uint"]) return MTLPixelFormatDepth32Float_Stencil8;
  return MTLPixelFormatInvalid;
}

static NSDictionary *loadJSON(NSString *path) {
  NSData *data = [NSData dataWithContentsOfFile:path];
  if (!data) return nil;
  return [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
}

static id<MTLLibrary> loadLibrary(id<MTLDevice> device, NSString *path, NSString **errorOut) {
  NSData *data = [NSData dataWithContentsOfFile:path];
  if (!data) {
    *errorOut = [NSString stringWithFormat:@"metallib not readable: %@", path];
    return nil;
  }
  dispatch_data_t dispatchData = dispatch_data_create([data bytes], [data length], dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NSError *error = nil;
  id<MTLLibrary> library = [device newLibraryWithData:dispatchData error:&error];
  if (!library) {
    *errorOut = error ? [error localizedDescription] : @"newLibraryWithData failed";
  }
  return library;
}

static id<MTLFunction> loadFunction(id<MTLDevice> device, NSDictionary *shader, NSString **errorOut) {
  NSString *path = stringValue(shader, @"metallib", @"");
  NSString *function = stringValue(shader, @"function", @"");
  id<MTLLibrary> library = loadLibrary(device, path, errorOut);
  if (!library) return nil;
  id<MTLFunction> fn = [library newFunctionWithName:function];
  if (!fn) {
    *errorOut = [NSString stringWithFormat:@"function not found: %@ in %@", function, path];
  }
  return fn;
}

static NSDictionary *okResult(NSDictionary *pipeline, NSString *status, NSDictionary *extra) {
  NSMutableDictionary *result = [@{
    @"name": stringValue(pipeline, @"name", @""),
    @"type": stringValue(pipeline, @"type", @""),
    @"ok": @YES,
    @"status": status,
  } mutableCopy];
  if (extra) [result addEntriesFromDictionary:extra];
  return result;
}

static NSDictionary *failResult(NSDictionary *pipeline, NSString *status, NSString *message) {
  return @{
    @"name": stringValue(pipeline, @"name", @""),
    @"type": stringValue(pipeline, @"type", @""),
    @"ok": @NO,
    @"status": status,
    @"error": message ? message : @"unknown error",
  };
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    if (argc < 2) {
      fprintf(stderr, "usage: offline_pso_factory <manifest.json>\n");
      return 2;
    }
    NSString *manifestPath = [NSString stringWithUTF8String:argv[1]];
    NSDictionary *manifest = loadJSON(manifestPath);
    if (![manifest isKindOfClass:[NSDictionary class]]) {
      fprintf(stderr, "manifest is not valid JSON object\n");
      return 2;
    }

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
      return 2;
    }

    NSArray *pipelines = manifest[@"pipelines"];
    NSMutableArray *results = [NSMutableArray array];
    NSUInteger failures = 0;

    for (NSDictionary *pipeline in pipelines) {
      if (![pipeline isKindOfClass:[NSDictionary class]]) continue;
      NSString *type = stringValue(pipeline, @"type", @"");
      NSString *errorText = nil;

      if ([type isEqualToString:@"function"]) {
        id<MTLFunction> fn = loadFunction(device, pipeline[@"shader"], &errorText);
        if (fn) {
          [results addObject:okResult(pipeline, @"function_loaded", @{@"function_type": @((NSUInteger)[fn functionType])})];
        } else {
          failures += 1;
          [results addObject:failResult(pipeline, @"function_failed", errorText)];
        }
      } else if ([type isEqualToString:@"compute"]) {
        id<MTLFunction> fn = loadFunction(device, pipeline[@"shader"], &errorText);
        if (!fn) {
          failures += 1;
          [results addObject:failResult(pipeline, @"function_failed", errorText)];
          continue;
        }
        NSError *error = nil;
        id<MTLComputePipelineState> pso = [device newComputePipelineStateWithFunction:fn error:&error];
        if (pso) {
          [results addObject:okResult(pipeline, @"compute_pso_created", @{@"thread_execution_width": @([pso threadExecutionWidth])})];
        } else {
          failures += 1;
          [results addObject:failResult(pipeline, @"compute_pso_failed", error ? [error localizedDescription] : @"newComputePipelineStateWithFunction failed")];
        }
      } else if ([type isEqualToString:@"render"]) {
        id<MTLFunction> vertex = loadFunction(device, pipeline[@"vertex"], &errorText);
        if (!vertex) {
          failures += 1;
          [results addObject:failResult(pipeline, @"vertex_function_failed", errorText)];
          continue;
        }
        id<MTLFunction> fragment = nil;
        id fragmentShader = pipeline[@"fragment"];
        if ([fragmentShader isKindOfClass:[NSDictionary class]]) {
          fragment = loadFunction(device, fragmentShader, &errorText);
          if (!fragment) {
            failures += 1;
            [results addObject:failResult(pipeline, @"fragment_function_failed", errorText)];
            continue;
          }
        }
        MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fragment;
        desc.rasterSampleCount = uintValue(pipeline, @"sample_count", 1);
        NSArray *colorFormats = pipeline[@"color_formats"];
        if (![colorFormats isKindOfClass:[NSArray class]] || [colorFormats count] == 0) {
          colorFormats = @[@"bgra8unorm"];
        }
        for (NSUInteger i = 0; i < [colorFormats count] && i < 8; i++) {
          desc.colorAttachments[i].pixelFormat = pixelFormat(colorFormats[i]);
        }
        desc.depthAttachmentPixelFormat = pixelFormat(stringValue(pipeline, @"depth_format", @"invalid"));
        desc.stencilAttachmentPixelFormat = pixelFormat(stringValue(pipeline, @"stencil_format", @"invalid"));
        NSError *error = nil;
        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (pso) {
          [results addObject:okResult(pipeline, @"render_pso_created", nil)];
        } else {
          failures += 1;
          [results addObject:failResult(pipeline, @"render_pso_failed", error ? [error localizedDescription] : @"newRenderPipelineStateWithDescriptor failed")];
        }
      } else if ([type isEqualToString:@"mesh_render"]) {
        failures += 1;
        [results addObject:failResult(pipeline, @"mesh_render_pso_not_implemented", @"offline mesh/object PSO creation is tracked but not wired in this harness yet")];
      } else {
        failures += 1;
        [results addObject:failResult(pipeline, @"unknown_pipeline_type", type)];
      }
    }

    NSDictionary *output = @{
      @"schema": @"metalsharp.d3d12-metal.offline-pso-factory.raw.v1",
      @"ok": @(failures == 0),
      @"device": [device name] ? [device name] : @"",
      @"pipeline_count": @([results count]),
      @"failure_count": @(failures),
      @"pipelines": results,
    };
    NSData *json = [NSJSONSerialization dataWithJSONObject:output options:NSJSONWritingPrettyPrinted error:nil];
    fwrite([json bytes], 1, [json length], stdout);
    fputc('\n', stdout);
    return failures == 0 ? 0 : 1;
  }
}
'''


def default_corpus_roots() -> list[Path]:
    roots = [
        Path.home() / ".metalsharp" / "shader-cache",
        Path("/Volumes/AverySSD/SteamLibrary/steamapps/common/Subnautica2/.metalsharp-cache/shader-cache"),
        Path("/tmp/dxmt_shader_cache"),
    ]
    return [root for root in roots if root.exists()]


def load_json(path: Path) -> dict | None:
    try:
        return json.loads(path.read_text())
    except Exception:
        return None


def discover_reflections(roots: list[Path]) -> list[Path]:
    reflections: list[Path] = []
    for root in roots:
        reflections.extend(root.rglob("*.json"))
    return sorted(set(path for path in reflections if path.with_suffix(".metallib").exists()))


def discover_pso_manifests(roots: list[Path]) -> list[Path]:
    manifests: list[Path] = []
    for root in roots:
        manifests.extend(root.rglob("pso-*.json"))
    return sorted(set(manifests))


def merged_pso_manifest(roots: list[Path], limit: int) -> dict | None:
    pipelines: list[dict] = []
    source_manifests: list[str] = []
    for manifest_path in discover_pso_manifests(roots):
        manifest = load_json(manifest_path)
        if not manifest or not isinstance(manifest.get("pipelines"), list):
            continue
        source_manifests.append(str(manifest_path))
        for pipeline in manifest["pipelines"]:
            if isinstance(pipeline, dict):
                copy = dict(pipeline)
                copy.setdefault("captured_manifest", str(manifest_path))
                pipelines.append(copy)
                if limit > 0 and len(pipelines) >= limit:
                    break
        if limit > 0 and len(pipelines) >= limit:
            break
    if not pipelines:
        return None
    return {
        "schema": "metalsharp.d3d12-metal.offline-pso-manifest.v1",
        "mode": "captured-pso-manifests",
        "roots": [str(root) for root in roots],
        "source_manifests": source_manifests,
        "pipelines": pipelines,
    }


def auto_manifest(roots: list[Path], limit: int) -> dict:
    pipelines: list[dict] = []
    for reflection_path in discover_reflections(roots):
        reflection = load_json(reflection_path)
        if not reflection:
            continue
        shader_type = str(reflection.get("ShaderType", ""))
        entry = str(reflection.get("EntryPoint", ""))
        if not shader_type or not entry:
            continue
        base = reflection_path.with_suffix("")
        shader = {
            "metallib": str(reflection_path.with_suffix(".metallib")),
            "function": entry,
            "reflection": str(reflection_path),
        }
        if shader_type == "Compute":
            pipelines.append({"name": base.name, "type": "compute", "shader": shader})
        elif shader_type in {"Vertex", "Fragment", "Object", "Mesh"}:
            pipelines.append({"name": base.name, "type": "function", "shader": shader})
        if limit > 0 and len(pipelines) >= limit:
            break
    return {
        "schema": "metalsharp.d3d12-metal.offline-pso-manifest.v1",
        "mode": "auto-from-reflection",
        "pipelines": pipelines,
    }


def compile_harness(build_dir: Path) -> Path:
    source = build_dir / "offline_pso_factory.mm"
    binary = build_dir / "offline_pso_factory"
    source.write_text(HARNESS_SOURCE)
    command = [
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
    ]
    subprocess.check_call(command)
    return binary


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create Metal PSOs offline from captured pipeline manifests and converted shader blobs."
    )
    parser.add_argument("--manifest", help="Captured PSO manifest JSON.")
    parser.add_argument("--corpus", action="append", default=[], help="Directory containing .metallib/.json pairs.")
    parser.add_argument("--profile", default="metalsharp")
    parser.add_argument("--results-dir", default=str(Path(__file__).resolve().parents[1] / "results"))
    parser.add_argument("--limit", type=int, default=0, help="Optional max number of auto-discovered shader tests.")
    parser.add_argument("--allow-empty", action="store_true")
    parser.add_argument("--keep-build-dir", action="store_true")
    args = parser.parse_args()

    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)
    out_path = results_dir / f"offline-pso-factory-{args.profile}.json"

    if args.manifest:
        manifest = load_json(Path(args.manifest))
        if not manifest:
            print(f"invalid manifest: {args.manifest}", file=sys.stderr)
            return 2
    else:
        roots = [Path(path) for path in args.corpus] if args.corpus else default_corpus_roots()
        manifest = merged_pso_manifest(roots, args.limit)
        if manifest is None:
            manifest = auto_manifest(roots, args.limit)
            manifest["roots"] = [str(root) for root in roots]

    pipelines = manifest.get("pipelines", [])
    if not pipelines:
        result = {
            "schema": "metalsharp.d3d12-metal.offline-pso-factory.v1",
            "profile": args.profile,
            "ok": bool(args.allow_empty),
            "empty": True,
            "message": "No PSO manifest entries or converted shader metallibs found.",
        }
        out_path.write_text(json.dumps(result, indent=2) + "\n")
        print(out_path)
        return 0 if args.allow_empty else 1

    with tempfile.TemporaryDirectory(prefix="metalsharp-offline-pso-") as tmp:
        build_dir = Path(tmp)
        manifest_path = build_dir / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        binary = compile_harness(build_dir)
        completed = subprocess.run(
            [str(binary), str(manifest_path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            raw = json.loads(completed.stdout)
        except Exception:
            raw = {
                "ok": False,
                "failure_count": len(pipelines),
                "pipelines": [],
                "stdout": completed.stdout[-4000:],
            }
        result = {
            "schema": "metalsharp.d3d12-metal.offline-pso-factory.v1",
            "profile": args.profile,
            "ok": completed.returncode == 0 and bool(raw.get("ok")),
            "returncode": completed.returncode,
            "manifest_mode": manifest.get("mode", "explicit"),
            "pipeline_count": raw.get("pipeline_count", len(pipelines)),
            "failure_count": raw.get("failure_count", len(pipelines)),
            "device": raw.get("device", ""),
            "pipelines": raw.get("pipelines", []),
            "stderr_tail": completed.stderr[-4000:],
        }
        if args.keep_build_dir:
            keep = results_dir / f"offline-pso-factory-{args.profile}-build"
            keep.mkdir(parents=True, exist_ok=True)
            (keep / "offline_pso_factory.mm").write_text(HARNESS_SOURCE)
            (keep / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    out_path.write_text(json.dumps(result, indent=2) + "\n")
    print(out_path)
    if not result["ok"]:
        for failure in result.get("pipelines", [])[:20]:
            if not failure.get("ok"):
                print(f"offline PSO failed: {failure.get('name')} {failure.get('error')}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
