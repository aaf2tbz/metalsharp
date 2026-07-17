# Direct Xcode-Clang build for the committed C backend and C test executable.
CC := $(shell xcrun --find clang)
SDKROOT := $(shell xcrun --show-sdk-path)
DEPLOYMENT_TARGET ?= 13.0
OUT_DIR ?= ../build/c-backend
RUNTIME_SRCS := $(shell awk '{print "./" $$0}' manifests/runtime-sources.txt)
TEST_SRCS := $(shell awk '{print "./" $$0}' manifests/test-sources.txt)
NATIVE_OBJS := $(shell awk '{print "./" $$0}' manifests/native-objects.txt)
MAINTAINED_SRCS := $(shell awk '{print "./" $$0}' manifests/maintained-sources.txt)
MAINTAINED_TEST_SRCS := $(shell awk '{print "./" $$0}' manifests/maintained-test-sources.txt)
RUNTIME_OBJS := $(patsubst ./runtime/c/%.c,build/runtime/%.o,$(RUNTIME_SRCS))
TEST_OBJS := $(patsubst ./tests/c/%.c,build/tests/%.o,$(TEST_SRCS))
MAINTAINED_OBJS := $(patsubst ./%.c,build/maintained/%.o,$(MAINTAINED_SRCS))
INSTALLER_TEST := $(OUT_DIR)/metalsharp-installer-tests
DXMT_SURFACE_TEST := $(OUT_DIR)/metalsharp-dxmt-surface-tests
POLICY_TEST := $(OUT_DIR)/metalsharp-policy-tests
CFLAGS := -std=c11 -w -Werror=implicit-function-declaration -fwrapv -fno-strict-aliasing \
  -funwind-tables -fno-stack-protector -ffunction-sections -fdata-sections \
  -fno-optimize-sibling-calls -fPIC -mno-outline-atomics -O1 -arch arm64 \
  -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT)
LDFLAGS := -arch arm64 -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT) \
  -Wl,-dead_strip -lobjc -framework Foundation -framework Security \
  -framework CoreFoundation -liconv -lc -lm
BACKEND := $(OUT_DIR)/metalsharp-backend
TEST_BIN := $(OUT_DIR)/metalsharp-backend-tests

.PHONY: integrity backend test-binary installer-test surface-test policy-test test smoke contract verify clean
integrity:
	@test "$$(wc -l < manifests/runtime-sources.txt | tr -d ' ')" = 374
	@test "$$(wc -l < manifests/test-sources.txt | tr -d ' ')" = 394
	@test "$$(wc -l < manifests/native-objects.txt | tr -d ' ')" = 38
	@test "$$(wc -l < manifests/maintained-sources.txt | tr -d ' ')" = 4
	@test "$$(wc -l < manifests/maintained-test-sources.txt | tr -d ' ')" = 3
	@while IFS= read -r source; do test -f "$$source"; done < manifests/runtime-sources.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/test-sources.txt
	@while IFS= read -r object; do file "$$object" | grep -q arm64; done < manifests/native-objects.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/maintained-sources.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/maintained-test-sources.txt
	@python3 ../../tools/c-backend/verify-module-alignment.py
	@python3 ../../tools/c-backend/verify-version-alignment.py
	@echo "C source manifests are complete (374 converted runtime, 394 converted test, 4 maintained runtime, 3 maintained test units)."

backend: $(BACKEND)
test-binary: $(TEST_BIN)

$(BACKEND): $(RUNTIME_OBJS) $(NATIVE_OBJS) $(MAINTAINED_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_OBJS) $(NATIVE_OBJS) $(MAINTAINED_OBJS) $(LDFLAGS) -o $@

$(TEST_BIN): $(TEST_OBJS) $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_OBJS) $(NATIVE_OBJS) $(LDFLAGS) -o $@

build/runtime/%.o: runtime/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/maintained/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(INSTALLER_TEST): bottles.c bottles.h installer.c installer.h launcher.c launcher.h runtime_surface.c runtime_surface.h tests/installer_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) bottles.c installer.c launcher.c runtime_surface.c tests/installer_test.c -o $@

installer-test: $(INSTALLER_TEST)
	"$(CURDIR)/$(INSTALLER_TEST)"

$(DXMT_SURFACE_TEST): runtime_surface.c runtime_surface.h tests/dxmt_surface_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) runtime_surface.c tests/dxmt_surface_test.c -o $@

surface-test: $(DXMT_SURFACE_TEST)
	"$(CURDIR)/../../tools/bundles/test-dxmt-surface-promotion.sh"

$(POLICY_TEST): bottles.c bottles.h launcher.c launcher.h runtime_surface.c runtime_surface.h tests/policy_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) bottles.c launcher.c runtime_surface.c tests/policy_test.c -o $@

policy-test: $(POLICY_TEST)
	"$(CURDIR)/$(POLICY_TEST)"

build/tests/%.o: tests/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	"$(CURDIR)/../../tools/c-backend/run-tests.sh" "$(CURDIR)/$(TEST_BIN)" "$(CURDIR)"

smoke: $(BACKEND)
	"$(CURDIR)/../../tools/c-backend/soak.sh" "$(CURDIR)/$(BACKEND)"

contract: $(BACKEND)
	python3 "$(CURDIR)/../../tools/contracts/validate-electron-backend-contract.py"
	python3 "$(CURDIR)/../../tools/contracts/run-electron-backend-conformance.py" "$(CURDIR)/$(BACKEND)"

verify: integrity backend installer-test surface-test policy-test test smoke contract

clean:
	rm -rf build $(OUT_DIR)
