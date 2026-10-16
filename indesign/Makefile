# Peppy for InDesign - the plug-in (macOS, arm64).
#   make                 build/Peppy.InDesignPlugin   (from the repo root: make plugin)
#   make install         into InDesign's Plug-Ins/Peppy (make that folder yours once - see the README)
#
# Builds the way the SDK's sample projects do, without Xcode: odfrc compiles
# Ppy.fr into the plug-in's resources, the plug-in's own C++ compiles with the
# SDK's defines and prefix header, and the rest of Peppy (shared/, panel/)
# compiles on its own.

ROOT    := ..
ID_SDK  ?= $(HOME)/Developer/AdobeInDesignSDK
ID_APP  ?= /Applications/Adobe InDesign 2026
VERSION := $(shell cat $(ROOT)/VERSION)
# The re-signed resource compiler (see the README), else the SDK's own.
ODFRC   ?= $(firstword $(wildcard $(ID_SDK)/devtools/bin/odfrc-x) $(ID_SDK)/devtools/bin/odfrc-cmd)

include sdk_flags.mk

B       := $(ROOT)/build/id
OBJ     := $(B)/obj
BUNDLE  := $(ROOT)/build/Peppy.InDesignPlugin
VER_A   := $(BUNDLE)/Versions/A
EXE     := $(VER_A)/Peppy

PKG     := $(ID_SDK)/build/mac/release_cocoa64/packagefolder/contents
CXX     := clang++
ARCH    := -target arm64-apple-macos13.0
SYSROOT := -isysroot $(shell xcrun --show-sdk-path)

# The plug-in's SDK side: as the SDK samples compile (Objective-C++, the prefix header).
ID_FLAGS := $(ARCH) $(SYSROOT) -x objective-c++ -std=gnu++17 -stdlib=libc++ -fpascal-strings -O2 -g \
	-fvisibility=hidden -fvisibility-inlines-hidden -fno-threadsafe-statics -fstack-protector \
	-Wno-deprecated-declarations -Wno-four-char-constants -Wno-unknown-pragmas -Wno-c++11-narrowing -Wno-inconsistent-missing-override \
	-Wno-nullability-completeness -Wno-enum-constexpr-conversion -Wno-inline-new-delete -Wno-deprecated-builtins -Wno-unused-parameter -Wno-switch -Wno-missing-field-initializers \
	-DNDEBUG -DRELEASE -DID_UI_PRECOMPILE -DNS_BUILD_32_LIKE_64 -DSUPPRESS_USING_BIB_NAMESPACE_IN_HEADER -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DBOOST_BIND_GLOBAL_PLACEHOLDERS \
	$(SDK_FLAGS) -I. -I$(ROOT)/build/id \
	-include $(ID_SDK)/source/precomp/gcc/PluginPrefix.pch
# Peppy's own code: plain C++17 / Objective-C++ with ARC, no SDK.
PEPPY_FLAGS := $(ARCH) $(SYSROOT) -std=c++17 -stdlib=libc++ -O2 -g -fvisibility=hidden -Wall -Wno-unused-parameter \
	-DPEPPY_VERSION=\"$(VERSION)\" -I$(ROOT)/panel -I$(ROOT)/build

ID_SRC    := PpyID.cpp PpyNoStrip.cpp PpyPlugin.cpp PpyCommands.cpp PpyScript.cpp $(ID_SDK)/source/sdksamples/common/SDKPlugInEntrypoint.cpp
SHARED_SRC := $(addprefix $(ROOT)/shared/,Core.cpp Dispatch.cpp Narrate.cpp Mcp.cpp Server.cpp Catalog.cpp Paths.cpp)
PANEL_SRC := $(ROOT)/panel/PeppyPanelView.mm $(ROOT)/panel/Terminal.mm PpyPanel.mm PpyImage.mm
OBJECTS   := $(addprefix $(OBJ)/id_,$(notdir $(ID_SRC:.cpp=.o))) $(addprefix $(OBJ)/,$(notdir $(SHARED_SRC:.cpp=.o))) \
	$(addprefix $(OBJ)/,$(notdir $(PANEL_SRC:.mm=.o)))

ODFRC_FLAGS := -d RELEASE -d MACINTOSH -d __GNUC__ -d ID_DROVER_ENABLE=1 -d ID_COCOA_ENABLE=1 -d DVAUI_ENABLE_CSNEXT_SKIN=1 -d __APPLE__ \
	-i . -i $(B) $(patsubst -I%,-i %,$(filter -I%,$(SDK_FLAGS)))

.PHONY: all install clean
all: $(BUNDLE)/.signed

$(OBJ) $(B):
	mkdir -p $@

# peppy.jsx as a C++ string literal for PpyCommands.cpp.
$(B)/peppy.jsx.inc: peppy.jsx Makefile | $(B)
	python3 -c "import json; s=open('peppy.jsx').read(); print('\n'.join(json.dumps(l + '\n') + ',' for l in s.split('\n')))" > $@

# The version lives in one place, the repo's VERSION file.
$(B)/PpyVersion.h: $(ROOT)/VERSION | $(B)
	printf '#pragma once\n#define kPpyVersion "%s"\n' "$(VERSION)" > $@

$(OBJ)/id_PpyCommands.o: $(B)/peppy.jsx.inc
$(OBJ)/id_%.o: %.cpp PpyID.h PpyHost.h PpyPanel.h PpyImage.h $(B)/PpyVersion.h | $(OBJ)
	$(CXX) $(ID_FLAGS) -c $< -o $@
$(OBJ)/id_SDKPlugInEntrypoint.o: $(ID_SDK)/source/sdksamples/common/SDKPlugInEntrypoint.cpp $(B)/PpyVersion.h | $(OBJ)
	$(CXX) $(ID_FLAGS) -c $< -o $@
$(OBJ)/%.o: $(ROOT)/shared/%.cpp $(wildcard $(ROOT)/shared/*.h) | $(OBJ)
	$(CXX) $(PEPPY_FLAGS) -c $< -o $@
$(OBJ)/%.o: $(ROOT)/panel/%.mm $(wildcard $(ROOT)/panel/*.h) $(ROOT)/build/AgentLogos.inc | $(OBJ)
	$(CXX) $(PEPPY_FLAGS) -fobjc-arc -c $< -o $@
$(OBJ)/PpyPanel.o: PpyPanel.mm PpyPanel.h | $(OBJ)
	$(CXX) $(PEPPY_FLAGS) -fobjc-arc -c $< -o $@
$(OBJ)/PpyImage.o: PpyImage.mm PpyImage.h | $(OBJ)
	$(CXX) $(PEPPY_FLAGS) -fobjc-arc -c $< -o $@

$(ROOT)/build/AgentLogos.inc:
	$(MAKE) -C $(ROOT) build/AgentLogos.inc

# Resources: odfrc writes Ppy.fr.rsrc and the idrc_* folders InDesign reads.
$(B)/res/.done: Ppy.fr PpyID.h PpyFactoryList.h $(B)/PpyVersion.h | $(B)
	rm -rf $(B)/res && mkdir -p $(B)/res
	$(ODFRC) $(ODFRC_FLAGS) -o $(B)/res/Ppy.fr.rsrc Ppy.fr
	touch $@

$(EXE): $(OBJECTS)
	mkdir -p $(VER_A)
	$(CXX) $(ARCH) $(SYSROOT) -dynamiclib -stdlib=libc++ -fobjc-link-runtime -dead_strip \
		-install_name /Peppy.InDesignPlugin/Versions/A/Peppy -Xlinker -rpath -Xlinker @executable_path/../Frameworks -Xlinker -no_data_in_code_info \
		-F$(PKG)/macos -L$(PKG)/macos -L$(PKG)/macos/libs -F$(PKG)/Frameworks -L$(PKG)/Frameworks \
		$^ -framework InDesignModelAndUI -lPublicPlugIn -framework Cocoa -framework QuartzCore -framework WebKit -framework ImageIO -o $@

$(BUNDLE)/.signed: $(EXE) $(B)/res/.done Info.plist $(wildcard $(ROOT)/panel/resources/terminal/*)
	mkdir -p $(VER_A)/Resources
	rm -rf $(VER_A)/Resources/idrc_* $(VER_A)/Resources/*.idrc
	cp -R $(B)/res/idrc_* $(VER_A)/Resources/ 2>/dev/null || true
	cp $(B)/res/*.idrc $(VER_A)/Resources/ 2>/dev/null || true
	cp $(B)/res/Ppy.fr.rsrc $(VER_A)/Resources/Peppy.rsrc
	sed -e 's/@VERSION@/$(VERSION)/g' Info.plist > $(VER_A)/Resources/Info.plist
	rm -rf $(VER_A)/Resources/terminal && cp -R $(ROOT)/panel/resources/terminal $(VER_A)/Resources/terminal
	ln -sfh A $(BUNDLE)/Versions/Current
	ln -sfh Versions/Current/Peppy $(BUNDLE)/Peppy
	ln -sfh Versions/Current/Resources $(BUNDLE)/Resources
	@# Like the SDK samples: the binary keeps the linker's ad hoc signature; the bundle is not signed.
	touch $@

install: all
	@test -w "$(ID_APP)/Plug-Ins/Peppy" || { echo "Make InDesign's Plug-Ins/Peppy folder yours first:"; \
		echo "  sudo mkdir -p \"$(ID_APP)/Plug-Ins/Peppy\" && sudo chown $$USER \"$(ID_APP)/Plug-Ins/Peppy\""; exit 1; }
	rm -rf "$(ID_APP)/Plug-Ins/Peppy/Peppy.InDesignPlugin"
	ditto $(BUNDLE) "$(ID_APP)/Plug-Ins/Peppy/Peppy.InDesignPlugin"

clean:
	rm -rf $(B) $(BUNDLE)
