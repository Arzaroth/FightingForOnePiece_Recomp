#!/bin/bash
# usage: dis.sh <start> <stop>  - disassemble the game ELF (FFOP_ELF) between two addresses
llvm-objdump -d --no-show-raw-insn --triple=mipsel --mcpu=mips3 --start-address=$1 --stop-address=$2 "${FFOP_ELF:-$(dirname "$0")/../game/SLPS_255.45}" | grep -E '^ +[0-9a-f]+:' | sed -E 's/<\.text\+0x[0-9a-f]+>//'
