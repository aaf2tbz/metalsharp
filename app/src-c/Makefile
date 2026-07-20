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
LAUNCH_ADAPTER_SRC := runtime/c/metalsharp_backend-be5c1ce78fdf154a.metalsharp_backend.821b1f24846112cc-cgu.03.rcgu.c
MIGRATION_ADAPTER_SRC := runtime/c/metalsharp_backend-be5c1ce78fdf154a.metalsharp_backend.821b1f24846112cc-cgu.08.rcgu.c
AGILITY_ADAPTER_SRC := runtime/c/metalsharp_backend-be5c1ce78fdf154a.metalsharp_backend.821b1f24846112cc-cgu.05.rcgu.c
BOTTLE_ADAPTER_SRC := runtime/c/metalsharp_backend-be5c1ce78fdf154a.metalsharp_backend.821b1f24846112cc-cgu.01.rcgu.c
RUNTIME_OBJS := $(patsubst ./runtime/c/%.c,build/runtime/%.o,$(RUNTIME_SRCS))
TEST_OBJS := $(patsubst ./tests/c/%.c,build/tests/%.o,$(TEST_SRCS))
MAINTAINED_OBJS := $(patsubst ./%.c,build/maintained/%.o,$(MAINTAINED_SRCS))
INSTALLER_TEST := $(OUT_DIR)/metalsharp-installer-tests
DXMT_SURFACE_TEST := $(OUT_DIR)/metalsharp-dxmt-surface-tests
POLICY_TEST := $(OUT_DIR)/metalsharp-policy-tests
BOTTLE_DEPLOYMENT_TEST := $(OUT_DIR)/metalsharp-bottle-deployment-tests
CFLAGS := -std=c11 -w -Werror=implicit-function-declaration -fwrapv -fno-strict-aliasing \
  -funwind-tables -fno-stack-protector -ffunction-sections -fdata-sections \
  -fno-optimize-sibling-calls -fPIC -mno-outline-atomics -O1 -arch arm64 \
  -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT)
LDFLAGS := -arch arm64 -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT) \
  -Wl,-dead_strip -lobjc -framework Foundation -framework Security \
  -framework CoreFoundation -liconv -lc -lm
BACKEND := $(OUT_DIR)/metalsharp-backend
TEST_BIN := $(OUT_DIR)/metalsharp-backend-tests

.PHONY: integrity backend graphics-archive-contract test-binary installer-test surface-test policy-test bottle-deployment-test test smoke contract verify clean
integrity:
	@test "$$(wc -l < manifests/runtime-sources.txt | tr -d ' ')" = 374
	@test "$$(wc -l < manifests/test-sources.txt | tr -d ' ')" = 394
	@test "$$(wc -l < manifests/native-objects.txt | tr -d ' ')" = 38
	@test "$$(wc -l < manifests/maintained-sources.txt | tr -d ' ')" = 5
	@test "$$(wc -l < manifests/maintained-test-sources.txt | tr -d ' ')" = 4
	@while IFS= read -r source; do test -f "$$source"; done < manifests/runtime-sources.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/test-sources.txt
	@while IFS= read -r object; do file "$$object" | grep -q arm64; done < manifests/native-objects.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/maintained-sources.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/maintained-test-sources.txt
	@grep -q 'metalsharp_launcher_preflight(_arg0' "$(LAUNCH_ADAPTER_SRC)"
	@grep -q 'metalsharp_launcher_prepare_eac(_arg0' "$(LAUNCH_ADAPTER_SRC)"
	@grep -q 'extern bool metalsharp_migration_refresh_saved_bottles(void);' "$(MIGRATION_ADAPTER_SRC)"
	@grep -q 'Required Wine prefix updates complete; saved bottle DLLs unchanged.' "$(MIGRATION_ADAPTER_SRC)"
	@! grep -q 'if (!metalsharp_migration_refresh_saved_bottles())' "$(MIGRATION_ADAPTER_SRC)"
	@grep -q 'metalsharp_game_requires_agility(_arg1, _arg2)' "$(AGILITY_ADAPTER_SRC)"
	@grep -q 'metalsharp_find_bytes(_arg0, _arg1, _arg2, _arg3' "$(AGILITY_ADAPTER_SRC)"
	@grep -q '_metalsharp_m12_components' "$(BOTTLE_ADAPTER_SRC)"
	@grep -q '(void \*)_metalsharp_m12_components' "$(BOTTLE_ADAPTER_SRC)"
	@grep -q '\*(__rustc_usize \*)_t41016 = 10ULL' "$(BOTTLE_ADAPTER_SRC)"
	@! sed -n '/_metalsharp_m12_components\[\]/,/^};/p' "$(BOTTLE_ADAPTER_SRC)" | grep -q '_bytes2971'
	@grep -q '!metalsharp_game_requires_agility(_t21441, _t21443)' "$(BOTTLE_ADAPTER_SRC)"
	@grep -q 'if (_arg4) goto bb_823_bb1364' "$(BOTTLE_ADAPTER_SRC)"
	@python3 ../../tools/c-backend/verify-module-alignment.py
	@python3 ../../tools/c-backend/verify-version-alignment.py
	@echo "C source manifests are complete (374 converted runtime, 394 converted test, 5 maintained runtime, 4 maintained test units)."

backend: $(BACKEND)

graphics-archive-contract: $(BACKEND)
	@strings "$(BACKEND)" | grep -q 'Graphics/dll/dxmt_m12/x86_64-windows/d3d12.dll'
	@strings "$(BACKEND)" | grep -q 'Graphics/dll/dxmt_m12/x86_64-unix/winemetal.so'
	@! strings "$(BACKEND)" | grep -q 'Graphics/dll/dxmt-m12/'
	@echo "Backend graphics archive contract uses the canonical Graphics/dll/dxmt_m12 lane."

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

$(BOTTLE_DEPLOYMENT_TEST): bottles.c bottles.h installer.c installer.h launcher.c launcher.h migration.c migration.h runtime_surface.c runtime_surface.h tests/bottle_deployment_test.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) bottles.c installer.c launcher.c migration.c runtime_surface.c tests/bottle_deployment_test.c -o $@

bottle-deployment-test: $(BOTTLE_DEPLOYMENT_TEST) $(DXMT_SURFACE_TEST)
	"$(CURDIR)/../../tools/bundles/test-bottle-deployment.sh"

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

verify: integrity backend graphics-archive-contract installer-test surface-test policy-test bottle-deployment-test test smoke contract

# ── Genuine-C backend (hand-written C11, replaces transpiled .rcgu.c) ──
GENUINE_SRCS := $(shell cat manifests/genuine-sources.txt)
GENUINE_OBJS := $(patsubst runtime/genuine/%.c,build/genuine/%.o,$(GENUINE_SRCS))
GENUINE_BACKEND := $(OUT_DIR)/metalsharp-backend-genuine

genuine-backend: $(GENUINE_BACKEND)

$(GENUINE_BACKEND): $(GENUINE_OBJS) $(NATIVE_OBJS) $(MAINTAINED_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(GENUINE_OBJS) $(NATIVE_OBJS) $(MAINTAINED_OBJS) $(LDFLAGS) -o $@

build/genuine/%.o: runtime/genuine/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I runtime/genuine -I . -c $< -o $@

genuine-contract: $(GENUINE_BACKEND)
	python3 "$(CURDIR)/../../tools/contracts/validate-electron-backend-contract.py"
	python3 "$(CURDIR)/../../tools/contracts/run-electron-backend-conformance.py" "$(CURDIR)/$(GENUINE_BACKEND)"

genuine-verify: genuine-backend genuine-contract
	@echo "Genuine-C backend verified: binary builds and passes contract conformance"

clean:
	rm -rf build $(OUT_DIR)
