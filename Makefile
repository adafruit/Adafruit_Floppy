# SPDX-FileCopyrightText: 2022 Jeff Epler for Adafruit Industries
#
# SPDX-License-Identifier: MIT

mfm: main.c mfm_impl.h
	gcc -o $@ $<

.PHONY: test
test: mfm
	./mfm < flux.txt
