# Peppy - macOS builds.
#   make mock      the server + catalog on a fake document (no SDK needed): build/peppy-mock
#   make test      build the mock and run the protocol tests against it
#   make plugin    the InDesign plug-in (needs the SDK at $(ID_SDK))  - see indesign/

VERSION := $(shell cat VERSION)
CXX     ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -DPEPPY_VERSION=\"$(VERSION)\"
ID_SDK  ?= $(HOME)/Developer/AdobeInDesignSDK

SHARED := shared/Core.cpp shared/Dispatch.cpp shared/Narrate.cpp shared/Mcp.cpp shared/Server.cpp shared/Catalog.cpp

.PHONY: all mock test plugin clean
all: mock

build/peppy-mock: $(SHARED) mock/MockHost.cpp shared/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SHARED) mock/MockHost.cpp -lpthread

mock: build/peppy-mock

test: build/peppy-mock
	python3 tests/test_protocol.py

plugin:
	$(MAKE) -C indesign ID_SDK="$(ID_SDK)"

clean:
	rm -rf build
