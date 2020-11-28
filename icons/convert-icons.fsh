#!/usr/bin/fish
for a in 48 64 72 96 128 160 192
set s "$a"x"$a"
set d (math $a \* 2.25)
convert -background none -density $d wstroke.svg $s/wstroke.png
end



