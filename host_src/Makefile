PYTHON3 = python3

.PHONY: check
check: main main_fm check_flux.py
	./main
	$(PYTHON3) check_flux.py flux0 > decode0
	$(PYTHON3) check_flux.py flux1 > decode1
	$(PYTHON3) check_flux.py flux2 > decode2
	./main_fm
	$(PYTHON3) check_flux.py --fm fluxfm > decodefm

main: main.c ../src/mfm_impl.h Makefile test_flux.h
	gcc -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $<

main_fm: main_fm.c ../src/mfm_impl.h Makefile
	gcc -iquote ../src -Wall -Werror -ggdb3 -Og -o $@ $< -fsanitize=address

test_flux.h: make_flux.py greaseweazle/scripts/greaseweazle/version.py
	$(PYTHON3) $< $@

greaseweazle/scripts/greaseweazle/version.py:
	$(MAKE) -C greaseweazle
