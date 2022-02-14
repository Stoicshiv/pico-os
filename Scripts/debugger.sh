#!/bin/bash
set -e

GDB="arm-none-eabi-gdb"

if [[ "$PWD" != "/home/me/dev/pico-os/Build" ]]
then
    echo "error: script executed in unknown location"
    exit 1
fi

tmp="$(mktemp -d)"

cat > "$tmp/init.gdb" <<EOD
set confirm off
set code-cache off
set stack-cache off

set history save on
set history size unlimited
set history remove-duplicates 1

target extended-remote localhost:3333
file System.elf
EOD

"$GDB" -q -x "$tmp/init.gdb"
