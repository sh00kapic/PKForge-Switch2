.DEFAULT_GOAL := all
SOURCE := $(OUT)/src
GENERATED := $(addprefix engine_,$(shell seq 0 47))
OBJECTS := $(addprefix $(OUT)/,$(addsuffix .o,$(GENERATED) wasm-rt-impl wasm-rt-exceptions-impl wasm-rt-mem-impl))
all: $(OUT)/libforge-runtime.a
$(OUT)/libforge-runtime.a: $(OBJECTS)
	$(AR) rcs $@ $^
$(SOURCE)/.stamp: runtime-source.tar.xz
	mkdir -p $(SOURCE)
	tar xf $< -C $(SOURCE)
	touch $@
$(OUT)/%.o: $(SOURCE)/.stamp $(wildcard *.h)
	$(CC) $(ARCH) -O1 -fexceptions -funwind-tables -DWASM_RT_USE_MMAP=0 -DWASM_RT_USE_SEGUE=0 -DWASM_RT_STACK_DEPTH_COUNT=1 -DWASM_RT_MAX_CALL_STACK_DEPTH=2048 -I. -c $(SOURCE)/$*.c -o $@
