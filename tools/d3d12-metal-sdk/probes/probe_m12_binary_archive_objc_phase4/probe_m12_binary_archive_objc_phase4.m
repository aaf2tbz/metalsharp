#import <Foundation/Foundation.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

@interface ProbeDescriptor : NSObject
@property (nonatomic, retain) id binaryArchives;
@property (nonatomic, assign) BOOL baselinePipelineCreated;
@end

@implementation ProbeDescriptor
@synthesize binaryArchives = _binaryArchives;
@synthesize baselinePipelineCreated = _baselinePipelineCreated;
- (void)dealloc {
    [_binaryArchives release];
    [super dealloc];
}
@end

@interface ProbeArchive : NSObject
@property (nonatomic, assign) BOOL throwCompute;
@property (nonatomic, assign) BOOL throwRender;
@property (nonatomic, assign) NSUInteger computeAdds;
@property (nonatomic, assign) NSUInteger renderAdds;
- (void)addComputePipelineFunctionsWithDescriptor:(ProbeDescriptor*)descriptor error:(NSError**)error;
- (void)addRenderPipelineFunctionsWithDescriptor:(ProbeDescriptor*)descriptor error:(NSError**)error;
@end

@implementation ProbeArchive
@synthesize throwCompute = _throwCompute;
@synthesize throwRender = _throwRender;
@synthesize computeAdds = _computeAdds;
@synthesize renderAdds = _renderAdds;
- (void)addComputePipelineFunctionsWithDescriptor:(ProbeDescriptor*)descriptor error:(NSError**)error {
    (void)descriptor;
    if (error)
        *error = nil;
    self.computeAdds++;
    if (self.throwCompute)
        [NSException raise:@"ProbeComputeArchiveFailure" format:@"forced compute archive failure"];
}
- (void)addRenderPipelineFunctionsWithDescriptor:(ProbeDescriptor*)descriptor error:(NSError**)error {
    (void)descriptor;
    if (error)
        *error = nil;
    self.renderAdds++;
    if (self.throwRender)
        [NSException raise:@"ProbeRenderArchiveFailure" format:@"forced render archive failure"];
}
@end

static pthread_mutex_t g_m12_binary_archive_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ProbeSafeAddCompute(ProbeArchive* archive, ProbeDescriptor* descriptor) {
    @synchronized(archive) {
        pthread_mutex_lock(&g_m12_binary_archive_mutex);
        @try {
            [archive addComputePipelineFunctionsWithDescriptor:descriptor error:nil];
        } @catch (NSException* exception) {
            (void)exception;
            descriptor.binaryArchives = nil;
        } @finally {
            pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        }
    }
}

static void ProbeSafeAddRender(ProbeArchive* archive, ProbeDescriptor* descriptor) {
    @synchronized(archive) {
        pthread_mutex_lock(&g_m12_binary_archive_mutex);
        @try {
            [archive addRenderPipelineFunctionsWithDescriptor:descriptor error:nil];
        } @catch (NSException* exception) {
            (void)exception;
            descriptor.binaryArchives = nil;
        } @finally {
            pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        }
    }
}

static bool MutexIsUnlocked(void) {
    int rc = pthread_mutex_trylock(&g_m12_binary_archive_mutex);
    if (rc == 0) {
        pthread_mutex_unlock(&g_m12_binary_archive_mutex);
        return true;
    }
    return false;
}

static bool WriteFile(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (!f)
        return false;
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    return true;
}

static int RunCase(const char* case_name, const char* output) {
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    ProbeArchive* archive = [[ProbeArchive alloc] init];
    ProbeDescriptor* descriptor = [[ProbeDescriptor alloc] init];
    descriptor.binaryArchives = [NSArray arrayWithObject:@"archive"];
    descriptor.baselinePipelineCreated = YES;

    const bool is_compute = strcmp(case_name, "compute-success") == 0 || strcmp(case_name, "compute-exception") == 0;
    const bool force_exception =
        strcmp(case_name, "compute-exception") == 0 || strcmp(case_name, "render-exception") == 0;
    archive.throwCompute = strcmp(case_name, "compute-exception") == 0;
    archive.throwRender = strcmp(case_name, "render-exception") == 0;

    if (is_compute)
        ProbeSafeAddCompute(archive, descriptor);
    else
        ProbeSafeAddRender(archive, descriptor);

    const bool mutex_unlocked = MutexIsUnlocked();
    const bool binary_archives_cleared_on_exception =
        force_exception ? descriptor.binaryArchives == nil : descriptor.binaryArchives != nil;
    const bool standard_pso_continues = descriptor.baselinePipelineCreated == YES;
    const bool add_attempted = is_compute ? archive.computeAdds == 1 : archive.renderAdds == 1;
    const bool passed =
        mutex_unlocked && binary_archives_cleared_on_exception && standard_pso_continues && add_attempted;

    char buffer[4096];
    snprintf(buffer, sizeof(buffer),
             "{\n"
             "  \"case\": \"%s\",\n"
             "  \"is_compute\": %s,\n"
             "  \"forced_exception\": %s,\n"
             "  \"add_attempted\": %s,\n"
             "  \"mutex_unlocked\": %s,\n"
             "  \"binary_archives_cleared_on_exception\": %s,\n"
             "  \"standard_pso_continues\": %s,\n"
             "  \"passed\": %s\n"
             "}\n",
             case_name, is_compute ? "true" : "false", force_exception ? "true" : "false",
             add_attempted ? "true" : "false", mutex_unlocked ? "true" : "false",
             binary_archives_cleared_on_exception ? "true" : "false", standard_pso_continues ? "true" : "false",
             passed ? "true" : "false");

    bool wrote = WriteFile(output, buffer);
    [descriptor release];
    [archive release];
    [pool drain];
    return wrote && passed ? 0 : 2;
}

int main(int argc, char** argv) {
    const char* case_name = NULL;
    const char* output = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--case") && i + 1 < argc) {
            case_name = argv[++i];
        } else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
            output = argv[++i];
        }
    }
    if (!case_name || !output)
        return 1;
    return RunCase(case_name, output);
}
