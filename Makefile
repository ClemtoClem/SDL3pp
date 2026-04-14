# ============================================================
#  Improved SDL3pp Makefile
# ============================================================

TARGET_NAME := Game
LIB_NAME	:= SDL3pp

CXX		  	:= g++
CXX_VERSION := 23

INCDIR	   	:= include
SRCDIR	  	:= src
BUILDDIR	:= build

LIB_TARGET  := $(BUILDDIR)/lib$(LIB_NAME).a
APP_TARGET  := $(TARGET_NAME)

EXTS	:= c cpp
SOURCES := $(foreach ext,$(EXTS),$(shell find $(SRCDIR) -type f -name '*.$(ext)' 2>/dev/null))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(basename $(SOURCES)))
OBJECTS := $(addsuffix .o,$(OBJECTS))

# ── Flags ───────────────────────────────────────────────────
CXXFLAGS := -std=c++$(CXX_VERSION) -Wall -Wextra -g -O0 -MMD -MP
CPPFLAGS := -I$(INCDIR)

LDLIBS := $(shell pkg-config --libs sdl3 sdl3-image sdl3-mixer sdl3-ttf sdl3-net 2>/dev/null) \
		  $(shell pkg-config --libs vulkan 2>/dev/null)

# ── Colours ─────────────────────────────────────────────────
GREEN  := $(shell tput setaf 2 2>/dev/null)
YELLOW := $(shell tput setaf 3 2>/dev/null)
RESET  := $(shell tput sgr0  2>/dev/null)

define print_green
	@echo "$(GREEN)$(1)$(RESET)"
endef
define print_yellow
	@echo "$(YELLOW)$(1)$(RESET)"
endef

# ── Targets ─────────────────────────────────────────────────
.PHONY: main all clean shaders examples

main: shaders $(APP_TARGET)
all: main examples

# ── Build directories ───────────────────────────────────────
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# ── Object compilation (Main Project) ───────────────────────
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -x c -c $< -o $@

# ── Static library ──────────────────────────────────────────
$(LIB_TARGET): $(OBJECTS)
	$(call print_green,"Archiving $(LIB_NAME)...")
	@ar rcs $@ $^

# ── Main executable ─────────────────────────────────────────
$(APP_TARGET): $(LIB_TARGET)
	$(call print_green,"Linking $(TARGET_NAME)...")
	@$(CXX) -o $@ $(OBJECTS) $(LDLIBS)

# ── Examples ────────────────────────────────────────────────
EXAMPLE_SRCS := $(shell find examples -name '*.cpp' 2>/dev/null)
EXAMPLE_OBJS := $(patsubst examples/%.cpp,$(BUILDDIR)/examples/%.o,$(EXAMPLE_SRCS))
EXAMPLE_BINS := $(patsubst examples/%.cpp,$(BUILDDIR)/examples/%,$(EXAMPLE_SRCS))

examples: $(EXAMPLE_BINS)

# 1. Compilation des objets pour les exemples (génère aussi les .d)
$(BUILDDIR)/examples/%.o: examples/%.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling example $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# 2. Édition de liens (linking) des exemples
$(BUILDDIR)/examples/%: $(BUILDDIR)/examples/%.o $(LIB_TARGET)
	$(call print_green,"Linking example $@")
	@$(CXX) $< -L$(BUILDDIR) -l$(LIB_NAME) $(LDLIBS) -o $@

# ── Shaders ────────────────────────────────────────────────
GLSLC		   := glslc
SPIRV_CROSS	 := spirv-cross

SHADER_SRCDIR   := assets/shaders/src
SHADER_BINDIR   := assets/shaders/bin

GLSL_SOURCES := $(shell find $(SHADER_SRCDIR) -type f \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' \) 2>/dev/null)
TARGET_SPV   := $(patsubst $(SHADER_SRCDIR)/%,$(SHADER_BINDIR)/%.spv,$(GLSL_SOURCES))

shaders: $(TARGET_SPV)

$(SHADER_BINDIR):
	mkdir -p $(SHADER_BINDIR)

$(SHADER_BINDIR)/%.spv: $(SHADER_SRCDIR)/%
	@mkdir -p $(dir $@)
	$(call print_green,"Compiling shader $<")
	@$(GLSLC) -O $< -o $@

# ── Clean ──────────────────────────────────────────────────
clean:
	rm -rf $(BUILDDIR)

# ── Dependencies ───────────────────────────────────────────
# On inclut maintenant les dépendances du projet principal ET des exemples
-include $(OBJECTS:.o=.d) $(EXAMPLE_OBJS:.o=.d)