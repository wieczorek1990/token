#!/bin/bash

# Liczba procesów
NP="$1"
# Nazwy plików wyjściowych
out=out
log=log

# Modyfikatory tekstu
reset=0
bright=1
dim=2
underscore=4
blink=5
reverse=7
hidden=8

# Kolory tekstu
black=30
red=31
green=32
yellow=33
blue=34
magenta=35
cyan=36
white=37

# Kolory tła
bg_black=40
bg_red=41
bg_green=42
bg_yellow=43
bg_blue=44
bg_magenta=45
bg_cyan=46
bg_white=47

# Koloruje dane wyrażenia z pomocą egrep
function colorize {
  if [ $# -ne 4 -a $# -ne 3 ]; then
    echo 'Usage: colorize file regex color [background_color]'
    exit 1
  fi
  file="$1"
  search="$2"
  color="$3"
  grep_color="$bright;$color"
  if [ $# -eq 4 ]; then
    bg_color="$4"
    grep_color=$grep_color";$bg_color"
  fi
  for arg in `seq 1 $#`; do
    shift
  done
  export GREP_COLOR="$grep_color"
  cat "$file" | egrep --color=always "($search.*)|$" "${@}" > tmp
  mv tmp "$file"
}

# Wybrane kolorowanie wyrażeń
function colors {
  if [ $# -ne 1 ]; then
    echo 'Usage: colors file'
    exit 1
  fi
  in="$1"
  colorize "$in" 'ERROR' $red
  colorize "$in" 'DBG' $white
  colorize "$in" 'CS' $red
  colorize "$in" 'LS' $green
  colorize "$in" 'COMM' $blue
  colorize "$in" 'Wysłałem żeton' $yellow
  colorize "$in" 'Odebrałem żeton' $yellow
  colorize "$in" 'Pracuję' $cyan
  colorize "$in" 'Zepsułem się' $cyan
  colorize "$in" 'Jestem naprawiany' $cyan
  colorize "$in" 'Zostałem naprawiony' $cyan
  colorize "$in" 'Start' $magenta
  colorize "$in" 'Inicjuję sekwencję zakończenia' $magenta
  colorize "$in" 'Koniec' $magenta
  colorize "$in" 'Kończę' $magenta
}

find . -name "$log"'.*' -exec rm {} \;
mpirun -np "$NP" 'Debug/PR' "${@}" 2>&1
find . -name "$log"'.*' -exec cat {} \; > "$log"
cat "$log" | sort -t \t -k 1,1n -k 2,2n | cut -f 3- > "$out"
colors "$out"
less -R "$out"

