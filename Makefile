# ArduboyStreamFX build/test entry points. There is no generated game source:
# the example cart and descriptors are produced by tools/wav_to_adpcm.py.

CXX          ?= c++
ARDUINO_CLI  ?= arduino-cli
FQBN         ?= arduboy-homemade:avr:arduboy-fx
MINI_FQBN    ?= arduboy-homemade:avr:arduboy-mini
CURDIR       := $(shell pwd)

CPPFLAGS     += -Isrc
CXXFLAGS     ?= -std=c++17 -O2 -Wall -Wextra -Werror
HOST_BIN     := build/host/streamfx_test
HEADERS      := $(wildcard src/streamfx/*.hpp) src/ArduboyStreamFX.h
RELAX_FLAGS  ?= -mrelax -mcall-prologues
AVR_FLAGS    := --optimize-for-debug \
                --build-property compiler.cpp.extra_flags="$(RELAX_FLAGS)" \
                --build-property compiler.c.extra_flags="-mrelax" \
                --build-property compiler.c.elf.extra_flags="-mrelax"
AVR_SIZE     ?= $(firstword $(wildcard $(HOME)/Library/Arduino15/packages/arduino/tools/avr-gcc/*/bin/avr-size) $(shell command -v avr-size 2>/dev/null))

PROBE_DIR    := tst/avr/streamfx_probe
BASE_DIR     := tst/avr/baseline
PROBE_ELF    := build/avr/probe/streamfx_probe.ino.elf
BASE_ELF     := build/avr/baseline/baseline.ino.elf

.PHONY: help test test-tools gen-example example size mini device clean

help:
	@printf '%s\n' \
		'  make test         host C++ codec/ring/lifecycle tests' \
		'  make test-tools   Python encoder tests' \
		'  make gen-example  synth example WAV, ADPCM, descriptors and FX cart' \
		'  make example      compile examples/FxLoop for Arduboy FX' \
		'  make size         compile baseline/probe FX images and report bytes' \
		'  make mini         compile the probe for Arduboy Mini' \
		'  make device       compile the Ardens smoke suite (needs an emulator to run)' \
		'  make clean        remove build artifacts only'

test: $(HOST_BIN)
	./$(HOST_BIN)

$(HOST_BIN): tst/main.cpp tst/codec_test.hpp tst/player_test.hpp tst/test.hpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ tst/main.cpp

test-tools:
	python3 -m unittest discover -s tools/tests -p 'test_*.py' -v

gen-example:
	python3 tools/make_example_loop.py

example: gen-example
	@mkdir -p build/example
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" $(AVR_FLAGS) \
		--library "$(CURDIR)" \
		--build-path build/example/arduino \
		--output-dir build/example/out \
		examples/FxLoop

size:
	@mkdir -p build/avr/probe build/avr/baseline
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" $(AVR_FLAGS) \
		--build-path build/avr/baseline/arduino \
		--output-dir build/avr/baseline \
		$(BASE_DIR)
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" $(AVR_FLAGS) \
		--library "$(CURDIR)" \
		--build-path build/avr/probe/arduino \
		--output-dir build/avr/probe \
		$(PROBE_DIR)
	@set -e; \
	for pair in "baseline:$(BASE_ELF)" "streamfx:$(PROBE_ELF)"; do \
		name=$${pair%%:*}; elf=$${pair#*:}; \
		counts=$$($(AVR_SIZE) -A "$$elf" | awk '$$1==".text"{t=$$2} $$1==".data"{d=$$2} $$1==".bss"{b=$$2} END{print t+0, d+0, b+0}'); \
		set -- $$counts; text=$$1; data=$$2; bss=$$3; \
		printf '%s: flash=%d (.text=%d .data=%d) ram=%d (.data=%d .bss=%d)\n' \
			"$$name" $$((text+data)) $$text $$data $$((data+bss)) $$data $$bss; \
	done

mini:
	@mkdir -p build/avr/mini
	$(ARDUINO_CLI) compile --fqbn "$(MINI_FQBN)" $(AVR_FLAGS) \
		--library "$(CURDIR)" \
		--build-path build/avr/mini/arduino \
		--output-dir build/avr/mini \
		$(PROBE_DIR)

device: gen-example
	@mkdir -p build/device
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" --optimize-for-debug \
		--library "$(CURDIR)" \
		--build-property compiler.cpp.extra_flags="-I$(CURDIR) $(RELAX_FLAGS)" \
		--build-property compiler.c.extra_flags="-mrelax" \
		--build-property compiler.c.elf.extra_flags="-mrelax" \
		--build-path build/device/arduino \
		--output-dir build/device \
		tst/streamfx_smoke

clean:
	rm -rf build
