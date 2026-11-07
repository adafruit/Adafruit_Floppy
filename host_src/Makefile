PYTHON3 = python3
.DEFAULT_GOAL := all

# File fixtures are checked without the optional Greaseweazle dependency.
.PHONY: check-files
check-files: test_mfm_files test_mfm_bpb
	./test_mfm_files
	./test_mfm_bpb

test_mfm_files: test_mfm_files.c ../src/mfm_impl.h
	$(CC) -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $<

test_mfm_bpb: test_mfm_bpb.c ../src/mfm_bpb.h
	$(CC) -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $<

.PHONY: check-catalog
check-catalog:
	$(CXX) -std=c++11 -Wall -Wextra -Werror -I../src image_catalog.cpp ../src/Adafruit_FloppyImage.cpp -o /tmp/floppsy-image-catalog
	$(PYTHON3) test_image_catalog.py /tmp/floppsy-image-catalog

.PHONY: all
all: check checkfm

.PHONY: check-layout
check-layout:
	$(CC) -iquote ../src -Wall -Werror -ggdb3 -Og -o /tmp/floppy-test-layout test_track_layout.c
	/tmp/floppy-test-layout

.PHONY: check
check: main check_flux.py
	./main
	$(PYTHON3) check_flux.py flux0 > decode0
	$(PYTHON3) check_flux.py flux1 > decode1
	$(PYTHON3) check_flux.py flux2 > decode2

.PHONY: checkfm
checkfm: main_fm check_flux.py
	./main_fm
	$(PYTHON3) check_flux.py --fm fluxfm > decodefm

main: main.c ../src/mfm_impl.h Makefile test_flux.h
	gcc -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $<

main_fm: main_fm.c ../src/mfm_impl.h Makefile
	gcc -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $<

test_flux.h: make_flux.py greaseweazle/scripts/greaseweazle/version.py
	$(PYTHON3) $< $@

greaseweazle/scripts/greaseweazle/version.py:
	$(MAKE) -C greaseweazle

# Portable codecs: no Arduino installation or Greaseweazle checkout is needed.
# Use CODEC_FLAGS='-fsanitize=address,undefined -g' for sanitizer coverage.
CODEC_SOURCES = ../src/Adafruit_FloppyBitstream.cpp ../src/Adafruit_FloppyCodec.cpp ../src/Adafruit_FloppyScan.cpp
CODEC_HEADERS = ../src/Adafruit_FloppyBitstream.h ../src/Adafruit_FloppyCodec.h
CODEC_FLAGS ?= -O2

.PHONY: check-codecs
check-codecs: track_codec test_bitstream
	./test_bitstream
	$(PYTHON3) test_track_codecs.py ./track_codec

track_codec: track_codec.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

test_bitstream: test_bitstream.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

# Independent encoder oracle needs Greaseweazle on PYTHONPATH.
test_amiga_encode: test_amiga_encode.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

.PHONY: check-amiga-encode
check-amiga-encode: test_amiga_encode
	$(PYTHON3) test_amiga_encode.py ./test_amiga_encode

test_mac_encode: test_mac_encode.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

.PHONY: check-mac-encode
check-mac-encode: test_mac_encode track_codec
	$(PYTHON3) test_mac_encode.py ./test_mac_encode ./track_codec

test_c64_encode: test_c64_encode.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

.PHONY: check-c64-encode
check-c64-encode: test_c64_encode
	$(PYTHON3) test_c64_encode.py ./test_c64_encode

test_apple_encode: test_apple_encode.cpp $(CODEC_SOURCES) $(CODEC_HEADERS)
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< $(CODEC_SOURCES) -o $@

.PHONY: check-apple-encode
check-apple-encode: test_apple_encode
	$(PYTHON3) test_apple_encode.py ./test_apple_encode

# Synthetic INDEX jitter, raw-flux containers and conservative track sharing.
test_apple_archive: test_apple_archive.cpp ../src/Adafruit_FloppyArchive.cpp ../src/Adafruit_FloppyArchive.h ../src/Adafruit_FloppyBitstream.cpp ../src/Adafruit_FloppyBitstream.h
	$(CXX) -std=c++11 -Wall -Wextra -Werror $(CODEC_FLAGS) -I../src $< ../src/Adafruit_FloppyArchive.cpp ../src/Adafruit_FloppyBitstream.cpp -o $@

.PHONY: check-apple-archive
check-apple-archive: test_apple_archive
	./test_apple_archive
