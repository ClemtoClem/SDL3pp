# ============================================================
#  Improved SDL3pp Makefile
# ============================================================

# ── Détection de l'OS ──
ifeq ($(OS),Windows_NT)
    EXE := .exe
else
    EXE :=
endif

TARGET_NAME := MiniGames
LIB_NAME	:= SDL3pp

CXX		  	:= g++
CXX_VERSION := 23

INCDIR	   	:= include
SRCDIR	  	:= src
BUILDDIR	:= build

LIB_TARGET  := $(BUILDDIR)/lib$(LIB_NAME).a
APP_TARGET  := $(TARGET_NAME)$(EXE)

EXTS	:= c cpp
SOURCES := $(foreach ext,$(EXTS),$(shell find $(SRCDIR) -type f -name '*.$(ext)' 2>/dev/null))
OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(basename $(SOURCES)))
OBJECTS := $(addsuffix .o,$(OBJECTS))

# ── Flags ───────────────────────────────────────────────────
CXXFLAGS := -std=c++$(CXX_VERSION) -Wall -Wextra -g -O0 -MMD -MP
CPPFLAGS := -I$(INCDIR)

LDLIBS := $(shell pkg-config --libs sdl3 sdl3-image sdl3-mixer sdl3-ttf sdl3-net 2>/dev/null) \
		  $(shell pkg-config --libs vulkan 2>/dev/null) -lsqlite3

# FFmpeg libraries
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavutil libavcodec libavformat libswscale libswresample 2>/dev/null)
FFMPEG_LIBS   := $(shell pkg-config --libs   libavformat libavcodec libavutil libswscale libswresample 2>/dev/null)

# Lua 5.4 — used by the tile/game-engine editor
LUA_CFLAGS    := $(shell pkg-config --cflags lua5.4 2>/dev/null)
LUA_LIBS      := $(shell pkg-config --libs   lua5.4 2>/dev/null)

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
.PHONY: main all clean shaders examples docs doc-open

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
EXAMPLE_BINS := $(patsubst examples/%.cpp,$(BUILDDIR)/examples/%$(EXE),$(EXAMPLE_SRCS))

# 'make examples' construit d'abord la bibliothèque, puis tous les exemples.
examples: $(LIB_TARGET) $(EXAMPLE_BINS)

# Cibler un binaire directement (ex: make build/examples/renderer/13_ui)
# ne recompile QUE ce .cpp — aucune source de src/ n'est touchée.
# La bibliothèque doit déjà exister (lancez 'make' une première fois).
$(BUILDDIR)/examples/%$(EXE): $(BUILDDIR)/examples/%.o
	$(call print_green,"Linking example $@")
	@[ -f "$(LIB_TARGET)" ] || \
		{ echo "$(YELLOW)$(LIB_TARGET) introuvable — lancez 'make' d'abord.$(RESET)"; exit 1; }
	@$(CXX) $< -L$(BUILDDIR) -l$(LIB_NAME) $(LDLIBS) -o $@

$(BUILDDIR)/examples/%.o: examples/%.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling example $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

# Exemple avec FFmpeg (flags supplémentaires)
$(BUILDDIR)/examples/demo/06_media_player.o: examples/demo/06_media_player.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling FFmpeg $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(FFMPEG_CFLAGS) -DSDL3PP_ENABLE_TTF -c $< -o $@

$(BUILDDIR)/examples/demo/06_media_player$(EXE): $(BUILDDIR)/examples/demo/06_media_player.o
	$(call print_green,"Linking FFmpeg $@")
	@[ -f "$(LIB_TARGET)" ] || \
		{ echo "$(YELLOW)$(LIB_TARGET) introuvable — lancez 'make' d'abord.$(RESET)"; exit 1; }
	@$(CXX) $< -L$(BUILDDIR) -l$(LIB_NAME) $(LDLIBS) $(FFMPEG_LIBS) -o $@

# Tile editor — needs Lua 5.4 (script workspace + node-graph script blocks)
$(BUILDDIR)/examples/demo/03_tile_editor.o: examples/demo/03_tile_editor.cpp | $(BUILDDIR)
	@mkdir -p $(dir $@)
	$(call print_yellow,"Compiling Lua $<")
	@$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LUA_CFLAGS) -DSDL3PP_TILE_EDITOR_LUA -c $< -o $@

$(BUILDDIR)/examples/demo/03_tile_editor$(EXE): $(BUILDDIR)/examples/demo/03_tile_editor.o
	$(call print_green,"Linking Lua $@")
	@[ -f "$(LIB_TARGET)" ] || \
		{ echo "$(YELLOW)$(LIB_TARGET) introuvable — lancez 'make' d'abord.$(RESET)"; exit 1; }
	@$(CXX) $< -L$(BUILDDIR) -l$(LIB_NAME) $(LDLIBS) $(LUA_LIBS) -o $@

# ── Shaders ────────────────────────────────────────────────
GLSLC		   := glslc
SPIRV_CROSS	   := spirv-cross

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

# ── Documentation ──────────────────────────────────────────
docs-clean:
	rm Doxyfile

docs:
	@if [ ! -f Doxyfile ]; then \
		$(call print_green,"Creating default Doxygen configuration..."); \
		doxygen -g; \
		sed -i 's|^PROJECT_NAME.*|PROJECT_NAME = "$(LIB_NAME)"|' Doxyfile; \
		sed -i 's|^INPUT .*|INPUT = $(INCDIR) examples README.md|' Doxyfile; \
		sed -i 's|^PREDEFINED .*|PREDEFINED = SDL3PP_DOC|' Doxyfile; \
		sed -i 's|^EXAMPLE_PATH .*|EXAMPLE_PATH = examples|' Doxyfile; \
		sed -i 's|^EXAMPLE_RECURSIVE .*|EXAMPLE_RECURSIVE = YES|' Doxyfile; \
		sed -i 's|^RECURSIVE .*|RECURSIVE = YES|' Doxyfile; \
		sed -i 's|^OUTPUT_DIRECTORY .*|OUTPUT_DIRECTORY = docs|' Doxyfile; \
		sed -i 's|^GENERATE_LATEX .*|GENERATE_LATEX = NO|' Doxyfile; \
		sed -i 's|^EXTRACT_ALL .*|EXTRACT_ALL = YES|' Doxyfile; \
		sed -i 's|^USE_MDFILE_AS_MAINPAGE .*|USE_MDFILE_AS_MAINPAGE = README.md|' Doxyfile; \
		sed -i 's|^EXTENSION_MAPPING .*|EXTENSION_MAPPING = md=markdown|' Doxyfile; \
		sed -i 's|^MARKDOWN_SUPPORT .*|MARKDOWN_SUPPORT = YES|' Doxyfile; \
		sed -i 's|^HAVE_DOT .*|HAVE_DOT = YES|' Doxyfile; \
		sed -i 's|^CALL_GRAPH .*|CALL_GRAPH = YES|' Doxyfile; \
		sed -i 's|^CALLER_GRAPH .*|CALLER_GRAPH = YES|' Doxyfile; \
		sed -i 's|^CLASS_DIAGRAMS .*|CLASS_DIAGRAMS = YES|' Doxyfile; \
		sed -i 's|^DOT_GRAPH_MAX_NODES .*|DOT_GRAPH_MAX_NODES = 100|' Doxyfile; \
		sed -i 's|^GENERATE_TREEVIEW .*|GENERATE_TREEVIEW = YES|' Doxyfile; \
		sed -i 's|^EXTRACT_STATIC .*|EXTRACT_STATIC = NO|' Doxyfile; \
		sed -i 's|^EXTRACT_LOCAL_CLASSES .*|EXTRACT_LOCAL_CLASSES = NO|' Doxyfile; \
	fi
	$(call print_green,"Generating documentation with Doxygen...")
	@doxygen Doxyfile
	$(call print_green,"Documentation generated in ./docs/html")

doc-open:
	@xdg-open docs/html/index.html || open docs/html/index.html || echo "Ouvrez docs/html/index.html manuellement"

# ── Dependencies ───────────────────────────────────────────
-include $(OBJECTS:.o=.d) $(EXAMPLE_OBJS:.o=.d)