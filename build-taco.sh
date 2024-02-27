#!/bin/sh
make ydkb/kbdfans_taco75:vial
util/uf2conv.py ydkb_kbdfans_taco75_vial.bin -b 0x8004000 -c -f 0x9d5bcf10 -o ydkb_kbdfans_taco75_vial.uf2