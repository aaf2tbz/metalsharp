# Direct Xcode-Clang build for the committed C backend and C test executable.
CC := $(shell xcrun --find clang)
SDKROOT := $(shell xcrun --show-sdk-path)
DEPLOYMENT_TARGET ?= 13.0
OUT_DIR ?= ../build/c-backend
RUNTIME_SRCS := $(shell awk '{print "./" $$0}' manifests/runtime-sources.txt)
TEST_SRCS := $(shell awk '{print "./" $$0}' manifests/test-sources.txt)
NATIVE_OBJS := $(shell awk '{print "./" $$0}' manifests/native-objects.txt)
RUNTIME_OBJS := $(patsubst ./runtime/c/%.c,build/runtime/%.o,$(RUNTIME_SRCS))
TEST_OBJS := $(patsubst ./tests/c/%.c,build/tests/%.o,$(TEST_SRCS))
CFLAGS := -std=c11 -w -Werror=implicit-function-declaration -fwrapv -fno-strict-aliasing \
  -funwind-tables -fno-stack-protector -ffunction-sections -fdata-sections \
  -fno-optimize-sibling-calls -fPIC -mno-outline-atomics -O1 -arch arm64 \
  -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT)
LDFLAGS := -arch arm64 -mmacosx-version-min=$(DEPLOYMENT_TARGET) -isysroot $(SDKROOT) \
  -Wl,-dead_strip -lobjc -framework Foundation -framework Security \
  -framework CoreFoundation -liconv -lc -lm
BACKEND := $(OUT_DIR)/metalsharp-backend
TEST_BIN := $(OUT_DIR)/metalsharp-backend-tests

.PHONY: integrity backend test-binary test smoke contract verify clean
integrity:
	@test "$$(wc -l < manifests/runtime-sources.txt | tr -d ' ')" = 374
	@test "$$(wc -l < manifests/test-sources.txt | tr -d ' ')" = 394
	@test "$$(wc -l < manifests/native-objects.txt | tr -d ' ')" = 38
	@while IFS= read -r source; do test -f "$$source"; done < manifests/runtime-sources.txt
	@while IFS= read -r source; do test -f "$$source"; done < manifests/test-sources.txt
	@while IFS= read -r object; do file "$$object" | grep -q arm64; done < manifests/native-objects.txt
	@echo "C source manifests are complete (374 runtime units, 394 test units)."

backend: $(BACKEND)
test-binary: $(TEST_BIN)

$(BACKEND): $(RUNTIME_OBJS) $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(RUNTIME_OBJS) $(NATIVE_OBJS) $(LDFLAGS) -o $@

$(TEST_BIN): $(TEST_OBJS) $(NATIVE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_OBJS) $(NATIVE_OBJS) $(LDFLAGS) -o $@

build/runtime/%.o: runtime/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%.o: tests/c/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	"$(CURDIR)/../../tools/c-backend/run-tests.sh" "$(CURDIR)/$(TEST_BIN)" "$(CURDIR)/../src-rust"

smoke: $(BACKEND)
	"$(CURDIR)/../../tools/c-backend/soak.sh" "$(CURDIR)/$(BACKEND)"

contract: $(BACKEND)
	python3 "$(CURDIR)/../../tools/contracts/validate-electron-backend-contract.py"
	python3 "$(CURDIR)/../../tools/contracts/run-electron-backend-conformance.py" "$(CURDIR)/$(BACKEND)"

verify: integrity backend test smoke contract

clean:
	rm -rf build $(OUT_DIR)
