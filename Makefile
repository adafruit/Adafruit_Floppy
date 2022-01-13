# SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries
#
# SPDX-License-Identifier: MIT

mfm: stand/main.c mfm_impl.h
	gcc -iquote . -o $@ $<

.PHONY: test
test: mfm
	./mfm < flux.txt

.PHONY: clean
clean:
	rm -f mfm
