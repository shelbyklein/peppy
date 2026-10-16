# Peppy - macOS builds.
#   make mock      the server + catalog on a fake document (no SDK needed): build/peppy-mock
#   make test      build the mock and run the protocol tests against it
#   make preview   the panel (Peppy the hare) on the mock, live: build/PeppyPreview (--demo, --snapshot DIR)
#   make plugin    the InDesign plug-in (needs the SDK at $(ID_SDK))  - see indesign/

VERSION := $(shell cat VERSION)
CXX     ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -DPEPPY_VERSION=\"$(VERSION)\"
ID_SDK  ?= $(HOME)/Developer/AdobeInDesignSDK

SHARED := shared/Core.cpp shared/Dispatch.cpp shared/Narrate.cpp shared/Mcp.cpp shared/Server.cpp shared/Catalog.cpp shared/Paths.cpp

.PHONY: all mock test preview plugin clean
all: mock

build/peppy-mock: $(SHARED) mock/MockDocument.cpp mock/main.cpp shared/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SHARED) mock/MockDocument.cpp mock/main.cpp -lpthread

mock: build/peppy-mock

test: build/peppy-mock
	python3 tests/test_protocol.py

# The agents' logos for the panel (panel/resources/agents/*.svg).
panel/AgentLogos.inc: tools/agent_logos.py $(wildcard panel/resources/agents/*.svg)
	@mkdir -p build
	python3 tools/agent_logos.py panel/resources/agents $@

PANEL := panel/PeppyPanelView.mm panel/Terminal.mm
build/PeppyPreview: panel/AgentLogos.inc $(PANEL) panel/*.h panel/Preview.mm $(SHARED) mock/MockDocument.cpp mock/Mock.h shared/*.h
	$(CXX) $(CXXFLAGS) -fobjc-arc -Ipanel -Ibuild $(PANEL) panel/Preview.mm $(SHARED) mock/MockDocument.cpp \
		-framework Cocoa -framework QuartzCore -framework WebKit -o $@

preview: build/PeppyPreview
	./build/PeppyPreview --demo

plugin:
	$(MAKE) -C indesign ID_SDK="$(ID_SDK)"

clean:
	rm -rf build
