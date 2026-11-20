# Run from the Switch 2 project directory.
.DEFAULT_GOAL := test
BUILD := build/host-test
CPPFLAGS += -Imanaged/runtime -DWASM_RT_USE_MMAP=0 -DWASM_RT_USE_SEGUE=0 -DWASM_RT_STACK_DEPTH_COUNT=1 -DWASM_RT_MAX_CALL_STACK_DEPTH=2048 -Iinclude -Ivendor/include -Ivendor/include/enums -Ivendor/memecrypto \
 -Ivendor/pcg-cpp/include -Ivendor/qrcodegen -Ivendor/lz4 -Ivendor/libwebp
CXXFLAGS += -std=gnu++23 -O1 -MMD -MP
CFLAGS += -O1 -MMD -MP
CPPFILES := source/gift_album.cpp source/engine_host.cpp source/workspace.cpp source/editors.cpp source/ui.cpp source/library.cpp source/portrait_model.cpp source/webp_portrait.cpp tests/workspace.cpp $(wildcard vendor/source/*/*.cpp)
CFILES := $(wildcard vendor/memecrypto/*.c vendor/qrcodegen/*.c vendor/lz4/*.c vendor/libwebp/src/*/*.c)
OBJECTS := $(addprefix $(BUILD)/,$(CPPFILES:.cpp=.o) $(CFILES:.c=.o))

.PHONY: test
test: $(BUILD)/test
	$(BUILD)/test
	python3 tools/pack_notices.py --check

RUNTIME := $(abspath $(BUILD))/engine-build/libforge-runtime.a
$(RUNTIME): $(wildcard managed/runtime/*)
	$(MAKE) -C managed/runtime OUT=$(abspath $(BUILD))/engine-build CC="$(CC)" AR="$(AR)"

$(BUILD)/test: $(OBJECTS) $(RUNTIME)
	$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(OBJECTS:.o=.d)

.PHONY: test-sd-portraits
test-sd-portraits: $(BUILD)/sd-portraits
	$< "$(PORTRAITS)" "$(MANIFEST)"

$(BUILD)/sd-portraits: $(BUILD)/tests/sd_portraits.o $(filter-out $(BUILD)/tests/workspace.o,$(OBJECTS)) $(RUNTIME)
	$(CXX) $(LDFLAGS) $^ -o $@

-include $(BUILD)/tests/sd_portraits.d

$(BUILD)/engine-runner: $(BUILD)/tests/engine_runner.o $(filter-out $(BUILD)/tests/workspace.o,$(OBJECTS)) $(RUNTIME)
	$(CXX) $(LDFLAGS) $^ -o $@

-include $(BUILD)/tests/engine_runner.d
