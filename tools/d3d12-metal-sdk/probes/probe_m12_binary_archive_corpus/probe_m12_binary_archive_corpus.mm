#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <sys/stat.h>
#include <unistd.h>

static NSDictionary *loadJSON(NSString *path) {
  NSData *data = [NSData dataWithContentsOfFile:path];
  if (!data) return nil;
  id obj = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
  return [obj isKindOfClass:[NSDictionary class]] ? (NSDictionary *)obj : nil;
}

static NSString *stringValue(NSDictionary *dict, NSString *key, NSString *fallback) {
  id value = dict[key];
  return [value isKindOfClass:[NSString class]] ? (NSString *)value : fallback;
}

static NSDictionary *dictValue(NSDictionary *dict, NSString *key) {
  id value = dict[key];
  return [value isKindOfClass:[NSDictionary class]] ? (NSDictionary *)value : nil;
}

static NSArray *arrayValue(NSDictionary *dict, NSString *key) {
  id value = dict[key];
  return [value isKindOfClass:[NSArray class]] ? (NSArray *)value : nil;
}

static NSString *errorString(NSError *error) {
  if (!error) return @"";
  NSString *desc = [error localizedDescription];
  return desc ? desc : [error description];
}

static MTLPixelFormat pixelFormat(NSString *name) {
  if (![name isKindOfClass:[NSString class]] || [name length] == 0) return MTLPixelFormatInvalid;
  NSString *n = [name lowercaseString];
  if ([n isEqualToString:@"invalid"] || [n isEqualToString:@"unknown"]) return MTLPixelFormatInvalid;
  if ([n isEqualToString:@"r8unorm"] || [n isEqualToString:@"r8_unorm"]) return MTLPixelFormatR8Unorm;
  if ([n isEqualToString:@"rg8unorm"] || [n isEqualToString:@"r8g8_unorm"]) return MTLPixelFormatRG8Unorm;
  if ([n isEqualToString:@"r16unorm"] || [n isEqualToString:@"r16_unorm"]) return MTLPixelFormatR16Unorm;
  if ([n isEqualToString:@"r16float"] || [n isEqualToString:@"r16_float"]) return MTLPixelFormatR16Float;
  if ([n isEqualToString:@"rg16unorm"] || [n isEqualToString:@"r16g16_unorm"]) return MTLPixelFormatRG16Unorm;
  if ([n isEqualToString:@"rg16float"] || [n isEqualToString:@"r16g16_float"]) return MTLPixelFormatRG16Float;
  if ([n isEqualToString:@"rgba8unorm"] || [n isEqualToString:@"r8g8b8a8_unorm"]) return MTLPixelFormatRGBA8Unorm;
  if ([n isEqualToString:@"rgba8unorm_srgb"] || [n isEqualToString:@"r8g8b8a8_unorm_srgb"]) return MTLPixelFormatRGBA8Unorm_sRGB;
  if ([n isEqualToString:@"bgra8unorm"] || [n isEqualToString:@"b8g8r8a8_unorm"]) return MTLPixelFormatBGRA8Unorm;
  if ([n isEqualToString:@"bgra8unorm_srgb"] || [n isEqualToString:@"b8g8r8a8_unorm_srgb"]) return MTLPixelFormatBGRA8Unorm_sRGB;
  if ([n isEqualToString:@"rgba16unorm"] || [n isEqualToString:@"r16g16b16a16_unorm"]) return MTLPixelFormatRGBA16Unorm;
  if ([n isEqualToString:@"rgba16float"] || [n isEqualToString:@"r16g16b16a16_float"]) return MTLPixelFormatRGBA16Float;
  if ([n isEqualToString:@"rgb10a2unorm"] || [n isEqualToString:@"r10g10b10a2_unorm"]) return MTLPixelFormatRGB10A2Unorm;
  if ([n isEqualToString:@"rg11b10float"] || [n isEqualToString:@"r11g11b10_float"]) return MTLPixelFormatRG11B10Float;
  if ([n isEqualToString:@"r32float"] || [n isEqualToString:@"r32_float"]) return MTLPixelFormatR32Float;
  if ([n isEqualToString:@"r32uint"] || [n isEqualToString:@"r32_uint"]) return MTLPixelFormatR32Uint;
  if ([n isEqualToString:@"rgba32float"] || [n isEqualToString:@"r32g32b32a32_float"]) return MTLPixelFormatRGBA32Float;
  if ([n isEqualToString:@"depth16unorm"] || [n isEqualToString:@"d16_unorm"]) return MTLPixelFormatDepth16Unorm;
  if ([n isEqualToString:@"depth32float"] || [n isEqualToString:@"d32_float"]) return MTLPixelFormatDepth32Float;
  if ([n isEqualToString:@"depth24unorm_stencil8"] || [n isEqualToString:@"d24_unorm_s8_uint"]) return (MTLPixelFormat)255;
  if ([n isEqualToString:@"depth32float_stencil8"] || [n isEqualToString:@"d32_float_s8x24_uint"]) return MTLPixelFormatDepth32Float_Stencil8;
  return MTLPixelFormatInvalid;
}

static NSUInteger vertexFormatByteSize(NSUInteger format) {
  switch (format) {
    case MTLVertexFormatUChar2:
    case MTLVertexFormatChar2: return 2;
    case MTLVertexFormatUChar3:
    case MTLVertexFormatChar3: return 3;
    case MTLVertexFormatUChar4:
    case MTLVertexFormatChar4:
    case MTLVertexFormatUChar4Normalized:
    case MTLVertexFormatChar4Normalized:
    case MTLVertexFormatFloat:
    case MTLVertexFormatInt:
    case MTLVertexFormatUInt: return 4;
    case MTLVertexFormatUShort2:
    case MTLVertexFormatShort2:
    case MTLVertexFormatHalf2:
    case MTLVertexFormatFloat2:
    case MTLVertexFormatInt2:
    case MTLVertexFormatUInt2: return 8;
    case MTLVertexFormatFloat3:
    case MTLVertexFormatInt3:
    case MTLVertexFormatUInt3: return 12;
    case MTLVertexFormatFloat4:
    case MTLVertexFormatInt4:
    case MTLVertexFormatUInt4: return 16;
    default: return 16;
  }
}

static MTLVertexDescriptor *makeVertexDescriptor(NSDictionary *pipeline) {
  NSDictionary *layout = dictValue(pipeline, @"input_layout");
  NSArray *elements = layout ? arrayValue(layout, @"elements") : nil;
  if (![elements isKindOfClass:[NSArray class]] || [elements count] == 0) return nil;

  MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
  NSUInteger strides[31] = {0};
  BOOL any = NO;
  for (NSDictionary *element in elements) {
    if (![element isKindOfClass:[NSDictionary class]]) continue;
    id systemValue = element[@"system_value"];
    if ([systemValue respondsToSelector:@selector(boolValue)] && [systemValue boolValue]) continue;
    NSUInteger attr = [element[@"register"] respondsToSelector:@selector(unsignedIntegerValue)] ? [element[@"register"] unsignedIntegerValue] : NSUIntegerMax;
    NSUInteger slot = [element[@"table_index"] respondsToSelector:@selector(unsignedIntegerValue)] ? [element[@"table_index"] unsignedIntegerValue] : 0;
    NSUInteger offset = [element[@"offset"] respondsToSelector:@selector(unsignedIntegerValue)] ? [element[@"offset"] unsignedIntegerValue] : 0;
    NSUInteger fmt = [element[@"metal_format"] respondsToSelector:@selector(unsignedIntegerValue)] ? [element[@"metal_format"] unsignedIntegerValue] : MTLVertexFormatInvalid;
    if (attr >= 31 || slot >= 31 || fmt == MTLVertexFormatInvalid) continue;
    vd.attributes[attr].format = (MTLVertexFormat)fmt;
    vd.attributes[attr].offset = offset;
    vd.attributes[attr].bufferIndex = slot;
    NSString *klass = stringValue(element, @"class", @"per_vertex");
    vd.layouts[slot].stepFunction = [klass isEqualToString:@"per_instance"] ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
    NSUInteger stepRate = [element[@"step_rate"] respondsToSelector:@selector(unsignedIntegerValue)] ? [element[@"step_rate"] unsignedIntegerValue] : 1;
    vd.layouts[slot].stepRate = stepRate ? stepRate : 1;
    NSUInteger end = offset + vertexFormatByteSize(fmt);
    if (end > strides[slot]) strides[slot] = end;
    any = YES;
  }
  if (!any) return nil;
  for (NSUInteger i = 0; i < 31; i++) {
    if (strides[i]) vd.layouts[i].stride = strides[i];
  }
  return vd;
}

static id<MTLLibrary> makeLibrary(id<MTLDevice> device, NSDictionary *shader, NSMutableString *errorOut) {
  NSString *kind = stringValue(shader, @"input_kind", @"");
  NSString *path = stringValue(shader, @"input_path", @"");
  if ([path length] == 0) {
    [errorOut appendString:@"missing shader input_path"];
    return nil;
  }
  NSError *error = nil;
  if ([kind isEqualToString:@"metallib"]) {
    NSData *data = [NSData dataWithContentsOfFile:path];
    if (!data || [data length] == 0) {
      [errorOut appendFormat:@"missing metallib %@", path];
      return nil;
    }
    dispatch_data_t dispatchData = dispatch_data_create([data bytes], [data length], NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    id<MTLLibrary> lib = [device newLibraryWithData:dispatchData error:&error];
    if (!lib) [errorOut appendFormat:@"newLibraryWithData failed: %@", errorString(error)];
    return lib;
  }

  NSString *source = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&error];
  if (!source || [source length] == 0) {
    [errorOut appendFormat:@"missing msl %@: %@", path, errorString(error)];
    return nil;
  }
  MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
  id<MTLLibrary> lib = [device newLibraryWithSource:source options:options error:&error];
  if (!lib) [errorOut appendFormat:@"newLibraryWithSource failed: %@", errorString(error)];
  return lib;
}

static id<MTLFunction> makeFunction(id<MTLDevice> device, NSDictionary *shader, NSMutableString *errorOut) {
  id<MTLLibrary> lib = makeLibrary(device, shader, errorOut);
  if (!lib) return nil;
  NSArray *names = @[
    stringValue(shader, @"function", @""),
    @"main",
    @"cs_main",
    @"vs_main",
    @"ps_main"
  ];
  NSMutableSet *seen = [NSMutableSet set];
  for (NSString *name in names) {
    if (![name isKindOfClass:[NSString class]] || [name length] == 0 || [seen containsObject:name]) continue;
    [seen addObject:name];
    id<MTLFunction> fn = [lib newFunctionWithName:name];
    if (fn) return fn;
  }
  [errorOut appendFormat:@"function lookup failed requested=%@", stringValue(shader, @"function", @"")];
  return nil;
}

static MTLRenderPipelineDescriptor *makeRenderDescriptor(id<MTLDevice> device, NSDictionary *pipeline, NSMutableString *errorOut) {
  NSDictionary *vertexShader = dictValue(pipeline, @"vertex");
  NSDictionary *fragmentShader = dictValue(pipeline, @"fragment");
  id<MTLFunction> vertex = makeFunction(device, vertexShader, errorOut);
  if (!vertex) return nil;
  id<MTLFunction> fragment = nil;
  if (fragmentShader) {
    fragment = makeFunction(device, fragmentShader, errorOut);
    if (!fragment) return nil;
  }

  MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
  desc.vertexFunction = vertex;
  desc.fragmentFunction = fragment;
  desc.rasterizationEnabled = YES;
  NSUInteger sampleCount = [pipeline[@"sample_count"] respondsToSelector:@selector(unsignedIntegerValue)] ? [pipeline[@"sample_count"] unsignedIntegerValue] : 1;
  desc.rasterSampleCount = sampleCount ? sampleCount : 1;
  NSArray *formats = arrayValue(pipeline, @"color_formats");
  BOOL hasColor = NO;
  for (NSUInteger i = 0; i < 8; i++) {
    NSString *fmtName = (formats && i < [formats count] && [formats[i] isKindOfClass:[NSString class]]) ? formats[i] : @"invalid";
    MTLPixelFormat fmt = pixelFormat(fmtName);
    desc.colorAttachments[i].pixelFormat = fmt;
    if (fmt != MTLPixelFormatInvalid) {
      hasColor = YES;
      desc.colorAttachments[i].writeMask = MTLColorWriteMaskAll;
    }
  }
  desc.depthAttachmentPixelFormat = pixelFormat(stringValue(pipeline, @"depth_format", @"invalid"));
  desc.stencilAttachmentPixelFormat = pixelFormat(stringValue(pipeline, @"stencil_format", @"invalid"));
  if (!hasColor && desc.depthAttachmentPixelFormat == MTLPixelFormatInvalid && desc.stencilAttachmentPixelFormat == MTLPixelFormatInvalid) {
    [errorOut appendString:@"render descriptor has no attachments"];
    return nil;
  }
  MTLVertexDescriptor *vd = makeVertexDescriptor(pipeline);
  if (vd) desc.vertexDescriptor = vd;
  return desc;
}

static MTLComputePipelineDescriptor *makeComputeDescriptor(id<MTLDevice> device, NSDictionary *pipeline, NSMutableString *errorOut) {
  NSDictionary *shader = dictValue(pipeline, @"shader");
  id<MTLFunction> fn = makeFunction(device, shader, errorOut);
  if (!fn) return nil;
  MTLComputePipelineDescriptor *desc = [[MTLComputePipelineDescriptor alloc] init];
  desc.computeFunction = fn;
  return desc;
}

static id<MTLBinaryArchive> newArchive(id<MTLDevice> device, NSString *path, NSMutableString *errorOut) {
  MTLBinaryArchiveDescriptor *desc = [[MTLBinaryArchiveDescriptor alloc] init];
  if ([path length] > 0) desc.url = [NSURL fileURLWithPath:path];
  NSError *error = nil;
  id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
  if (!archive) [errorOut appendFormat:@"newBinaryArchive failed path=%@: %@", path ?: @"", errorString(error)];
  return archive;
}

static BOOL createPipeline(id<MTLDevice> device, NSDictionary *candidate, id<MTLBinaryArchive> lookupArchive, BOOL strict, NSMutableString *errorOut) {
  NSDictionary *pipeline = dictValue(candidate, @"pipeline");
  NSString *type = stringValue(candidate, @"type", @"");
  NSError *error = nil;
  MTLPipelineOption options = strict ? MTLPipelineOptionFailOnBinaryArchiveMiss : MTLPipelineOptionNone;

  if ([type isEqualToString:@"compute"]) {
    MTLComputePipelineDescriptor *desc = makeComputeDescriptor(device, pipeline, errorOut);
    if (!desc) return NO;
    if (lookupArchive) desc.binaryArchives = @[ lookupArchive ];
    MTLAutoreleasedComputePipelineReflection reflection = nil;
    id<MTLComputePipelineState> pso = [device newComputePipelineStateWithDescriptor:desc options:options reflection:&reflection error:&error];
    if (!pso) {
      [errorOut appendFormat:@"compute PSO failed strict=%d: %@", strict ? 1 : 0, errorString(error)];
      return NO;
    }
    return YES;
  }

  if ([type isEqualToString:@"render"]) {
    MTLRenderPipelineDescriptor *desc = makeRenderDescriptor(device, pipeline, errorOut);
    if (!desc) return NO;
    if (lookupArchive) desc.binaryArchives = @[ lookupArchive ];
    MTLAutoreleasedRenderPipelineReflection reflection = nil;
    id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc options:options reflection:&reflection error:&error];
    if (!pso) {
      [errorOut appendFormat:@"render PSO failed strict=%d: %@", strict ? 1 : 0, errorString(error)];
      return NO;
    }
    return YES;
  }

  [errorOut appendFormat:@"unsupported type %@", type];
  return NO;
}

static BOOL addFunctionsToArchive(id<MTLDevice> device, NSDictionary *candidate, id<MTLBinaryArchive> archive, NSMutableString *errorOut) {
  NSDictionary *pipeline = dictValue(candidate, @"pipeline");
  NSString *type = stringValue(candidate, @"type", @"");
  NSError *error = nil;
  if ([type isEqualToString:@"compute"]) {
    MTLComputePipelineDescriptor *desc = makeComputeDescriptor(device, pipeline, errorOut);
    if (!desc) return NO;
    BOOL ok = [archive addComputePipelineFunctionsWithDescriptor:desc error:&error];
    if (!ok) [errorOut appendFormat:@"addComputePipelineFunctions failed: %@", errorString(error)];
    return ok;
  }
  if ([type isEqualToString:@"render"]) {
    MTLRenderPipelineDescriptor *desc = makeRenderDescriptor(device, pipeline, errorOut);
    if (!desc) return NO;
    BOOL ok = [archive addRenderPipelineFunctionsWithDescriptor:desc error:&error];
    if (!ok) [errorOut appendFormat:@"addRenderPipelineFunctions failed: %@", errorString(error)];
    return ok;
  }
  [errorOut appendFormat:@"unsupported type %@", type];
  return NO;
}

static unsigned long long fileSize(NSString *path) {
  struct stat st;
  if (stat([path fileSystemRepresentation], &st) != 0) return 0;
  return (unsigned long long)st.st_size;
}

static void usage(const char *argv0) {
  fprintf(stderr, "usage: %s --input probe-input.json --archive archive.binarchive --output result.json\n", argv0);
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    NSString *inputPath = nil;
    NSString *archivePath = nil;
    NSString *outputPath = nil;
    for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--input") && i + 1 < argc) inputPath = [NSString stringWithUTF8String:argv[++i]];
      else if (!strcmp(argv[i], "--archive") && i + 1 < argc) archivePath = [NSString stringWithUTF8String:argv[++i]];
      else if (!strcmp(argv[i], "--output") && i + 1 < argc) outputPath = [NSString stringWithUTF8String:argv[++i]];
      else { usage(argv[0]); return 2; }
    }
    if (!inputPath || !archivePath || !outputPath) { usage(argv[0]); return 2; }

    NSDictionary *input = loadJSON(inputPath);
    NSArray *candidates = input ? arrayValue(input, @"candidates") : nil;
    if (![candidates isKindOfClass:[NSArray class]]) {
      fprintf(stderr, "invalid input manifest\n");
      return 2;
    }

    [[NSFileManager defaultManager] removeItemAtPath:archivePath error:nil];
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      fprintf(stderr, "MTLCreateSystemDefaultDevice failed\n");
      return 1;
    }

    NSMutableString *archiveError = [NSMutableString string];
    id<MTLBinaryArchive> archive = newArchive(device, nil, archiveError);
    NSMutableArray *results = [NSMutableArray array];

    NSUInteger addOK = 0, createOK = 0, initialOKCount = 0, strictOK = 0;
    for (NSDictionary *candidate in candidates) {
      NSMutableDictionary *record = [NSMutableDictionary dictionary];
      record[@"name"] = stringValue(candidate, @"name", @"");
      record[@"type"] = stringValue(candidate, @"type", @"");
      record[@"classification"] = stringValue(candidate, @"classification", @"");
      NSMutableString *addError = [NSMutableString string];
      BOOL add = archive ? addFunctionsToArchive(device, candidate, archive, addError) : NO;
      if (add) addOK++;
      record[@"archive_add_ok"] = @(add);
      record[@"archive_add_error"] = addError;

      NSMutableString *createError = [NSMutableString string];
      BOOL created = createPipeline(device, candidate, nil, NO, createError);
      if (created) createOK++;
      record[@"baseline_create_ok"] = @(created);
      record[@"baseline_create_error"] = createError;
      [results addObject:record];
    }

    NSError *serError = nil;
    BOOL serializeOK = archive ? [archive serializeToURL:[NSURL fileURLWithPath:archivePath] error:&serError] : NO;
    unsigned long long archiveBytes = fileSize(archivePath);

    NSMutableString *reloadError = [NSMutableString string];
    id<MTLBinaryArchive> reloaded = (serializeOK && archiveBytes > 0) ? newArchive(device, archivePath, reloadError) : nil;
    BOOL reloadOK = reloaded != nil;

    for (NSUInteger i = 0; i < [candidates count] && i < [results count]; i++) {
      NSDictionary *candidate = candidates[i];
      NSMutableDictionary *record = results[i];
      BOOL initialOK = [record[@"archive_add_ok"] boolValue] && [record[@"baseline_create_ok"] boolValue];
      NSMutableString *strictError = [NSMutableString string];
      BOOL strictCreate = NO;
      if (initialOK) initialOKCount++;
      if (initialOK && reloadOK) {
        strictCreate = createPipeline(device, candidate, reloaded, YES, strictError);
      } else if (!reloadOK) {
        [strictError appendFormat:@"archive reload failed: %@", reloadError];
      } else {
        [strictError appendString:@"skipped because archive add or baseline create failed"];
      }
      if (strictCreate) strictOK++;
      record[@"strict_lookup_create_ok"] = @(strictCreate);
      record[@"strict_lookup_error"] = strictError;
    }

    NSMutableDictionary *counts = [NSMutableDictionary dictionary];
    counts[@"selected_total"] = @([candidates count]);
    counts[@"archive_add_ok"] = @(addOK);
    counts[@"baseline_create_ok"] = @(createOK);
    counts[@"initial_archive_add_and_baseline_create_ok"] = @(initialOKCount);
    counts[@"strict_lookup_create_ok"] = @(strictOK);

    NSDictionary *out = @{
      @"schema": @"metalsharp.m12.binary_archive_corpus_probe.v1",
      @"input": inputPath,
      @"archive_path": archivePath,
      @"archive_serialize_ok": @(serializeOK),
      @"archive_serialize_error": errorString(serError),
      @"archive_bytes": @(archiveBytes),
      @"archive_reload_ok": @(reloadOK),
      @"archive_reload_error": reloadError,
      @"strict_lookup_mode": @"MTLPipelineOptionFailOnBinaryArchiveMiss",
      @"counts": counts,
      @"candidates": results,
    };

    NSData *json = [NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
    if (!json || ![json writeToFile:outputPath atomically:YES]) {
      fprintf(stderr, "failed to write output\n");
      return 1;
    }

    BOOL pass = serializeOK && archiveBytes > 0 && reloadOK && initialOKCount > 0 && strictOK == initialOKCount;
    return pass ? 0 : 1;
  }
}
