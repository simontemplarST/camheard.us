# hugofe -- a Dear ImGui front end for this Hugo site.
#
#   make            # build ./hugofe
#   make run        # build and launch it against the site above this dir
#   make test       # the parsing/editing layer, no window needed
#
# Everything is vendored (vendor/imgui) except SDL2 and libGL, which are
# probed rather than assumed -- see the two blocks below.
IMGUI_DIR := vendor/imgui
CXX ?= c++
CC ?= cc

# SDL2: honour an explicit PKG_CONFIG_PATH first, then add Homebrew/Linuxbrew's
# pkgconfig dir if there is one. On an rpm-ostree/atomic box SDL2 comes from
# Linuxbrew, whose pkgconfig dir is NOT on the default search path unless the
# brew shellenv has been sourced -- without this the build dies at
# "SDL.h: No such file or directory", which looks like a missing package
# rather than a missing search path.
BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
ifneq ($(BREW_PREFIX),)
export PKG_CONFIG_PATH := $(PKG_CONFIG_PATH):$(BREW_PREFIX)/lib/pkgconfig
endif
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
$(warning pkg-config found no sdl2 -- falling back to -lSDL2; set PKG_CONFIG_PATH if it lives somewhere unusual)
SDL_LIBS := -lSDL2
endif

# libGL: probed, not assumed. An rpm-ostree/atomic host carries libGL.so.1 at
# runtime but not the libGL.so development symlink (that is in
# mesa-libGL-devel, layering which costs a reboot), so plain -lGL fails with
# "cannot find -lGL" even though the library is right there. ld's -l:<file>
# form links the runtime soname directly and works on both kinds of box.
GL_LIB := $(shell if echo 'int main(void){return 0;}' | $(CC) -x c - -lGL -o /dev/null >/dev/null 2>&1; \
	then echo -lGL; else echo -l:libGL.so.1; fi)

CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -MMD -MP \
	-I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMGUI_DIR)/misc/cpp \
	$(SDL_CFLAGS)
LDFLAGS := $(SDL_LIBS) $(GL_LIB) -ldl -pthread
CXXFLAGS += -pthread

# ---- the app ---------------------------------------------------------------
TARGET := hugofe
APP_SOURCES := \
	src/main.cpp \
	src/app_content.cpp \
	src/app_editor.cpp \
	src/app_design.cpp \
	src/app_media.cpp \
	src/app_publish.cpp \
	src/md_render.cpp \
	src/shot.cpp \
	src/theme.cpp \
	src/widgets.cpp \
	src/proc.cpp \
	src/md.cpp \
	src/fm.cpp \
	src/site.cpp \
	src/toml_edit.cpp \
	src/util.cpp \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMGUI_DIR)/misc/cpp/imgui_stdlib.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp \
	$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
APP_OBJECTS := $(APP_SOURCES:.cpp=.o)

$(TARGET): $(APP_OBJECTS)
	$(CXX) $(APP_OBJECTS) $(LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# ---- tests -----------------------------------------------------------------
# No SDL, no GL, no window: the whole parsing/editing layer is plain strings
# and files, so this builds and runs anywhere the compiler does.
TEST_SOURCES := tests/core_test.cpp src/md.cpp src/fm.cpp src/site.cpp src/toml_edit.cpp src/util.cpp
TEST_OBJECTS := $(TEST_SOURCES:.cpp=.test.o)
TEST_TARGET := core_test

%.test.o: %.cpp
	$(CXX) -std=c++17 -O2 -Wall -Wextra -MMD -MP -c $< -o $@

$(TEST_TARGET): $(TEST_OBJECTS)
	$(CXX) $(TEST_OBJECTS) -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(APP_OBJECTS) $(TEST_OBJECTS)
	rm -f src/*.d tests/*.d $(IMGUI_DIR)/*.d $(IMGUI_DIR)/backends/*.d $(IMGUI_DIR)/misc/cpp/*.d

.PHONY: run test clean

# Kept at the very END on purpose: DEPFILES uses := (immediate expansion), so
# it has to come after every object list is defined or it silently resolves to
# an empty list and header changes stop triggering rebuilds -- which, with
# structs shared across translation units, is an ODR bug waiting to happen,
# not just a staleness annoyance.
DEPFILES := $(patsubst %.o,%.d,$(APP_OBJECTS) $(TEST_OBJECTS))
-include $(DEPFILES)
