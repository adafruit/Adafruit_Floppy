PYTHON3 = python3

.PHONY: check-catalog
check-catalog:
	$(CXX) -std=c++11 -Wall -Wextra -Werror -I../src image_catalog.cpp ../src/Adafruit_FloppyImage.cpp -o /tmp/floppsy-image-catalog
	$(PYTHON3) test_image_catalog.py /tmp/floppsy-image-catalog

.PHONY: all
all: check checkfm

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
