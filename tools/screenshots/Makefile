# Renders the HUD screens to docs/images/*.png using the real widget code.
#
#   make            build and write the PNGs
#   make clean
#
# Needs a C/C++ compiler, zlib and git (LVGL is cloned on first use).
# Point LVGL_DIR at an existing checkout to skip the clone.

LVGL_VERSION ?= 9.2.2
ROOT         := ../..
BUILD        := build
LVGL_DIR     ?= $(BUILD)/lvgl
OUT          ?= $(ROOT)/docs/images

INCLUDES := -Istubs -I$(ROOT)/config -I$(LVGL_DIR) \
            -I$(ROOT)/lib/hud_widgets -I$(ROOT)/lib/hud_proto
DEFINES  := -DLV_CONF_INCLUDE_SIMPLE
CFLAGS   += -O2 -w $(DEFINES) $(INCLUDES)
CXXFLAGS += -O2 -std=c++17 -Wall $(DEFINES) $(INCLUDES)

LVGL_STAMP := $(LVGL_DIR)/lvgl.h
LVGL_SRCS   = $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null)
LVGL_OBJS   = $(patsubst $(LVGL_DIR)/%.c,$(BUILD)/obj/%.o,$(LVGL_SRCS))
WIDGETS    := $(wildcard $(ROOT)/lib/hud_widgets/*.h $(ROOT)/lib/hud_proto/*.h)

.PHONY: all clean touch-test
# Sub-make so the LVGL source list is evaluated after the clone.
all: $(LVGL_STAMP)
	$(MAKE) $(BUILD)/render
	mkdir -p $(OUT)
	$(BUILD)/render $(OUT)

$(LVGL_STAMP):
	git clone --quiet --depth 1 --branch v$(LVGL_VERSION) https://github.com/lvgl/lvgl.git $(LVGL_DIR)

$(BUILD)/render: $(BUILD)/main.o $(LVGL_OBJS)
	@echo "link $@"
	@$(CXX) -o $@ $^ -lz

# Touch controls through LVGL with a scripted finger (tests/widgets/touch_test.cpp)
touch-test: $(LVGL_STAMP)
	$(MAKE) $(BUILD)/touch_test
	$(BUILD)/touch_test

$(BUILD)/touch_test: $(BUILD)/touch_test.o $(LVGL_OBJS)
	@echo "link $@"
	@$(CXX) -o $@ $^

$(BUILD)/touch_test.o: $(ROOT)/tests/widgets/touch_test.cpp $(ROOT)/config/lv_conf.h $(WIDGETS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/main.o: main.cpp stubs/Arduino.h $(ROOT)/config/lv_conf.h $(WIDGETS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: $(LVGL_DIR)/%.c $(ROOT)/config/lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
