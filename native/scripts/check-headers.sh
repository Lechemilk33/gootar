#!/usr/bin/env bash
# Compile every header on its own.
#
# A header that only builds because some earlier file happened to include
# <vector> is exactly what fails on a different compiler - which is how two
# CI rounds got spent on <windows.h> and <cstdlib>. Catching it here costs
# seconds; catching it in CI costs a round trip each time.
set -uo pipefail
cd "$(dirname "$0")/.."

INC="-Isrc -Isrc/dsp -Ilibs/AudioDSPTools/dsp -Ilibs/NeuralAudio \
     -Ilibs/NeuralAudio/deps/RTNeural/modules/Eigen \
     -Ilibs/NeuralAudio/deps/RTNeural/modules/json \
     -Ilibs/NeuralAudio/deps/math_approx/include"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fails=0
for h in $(find src -name '*.h' -not -path 'src/app/*' | sort); do
  printf '#include "%s"\nint main(){return 0;}\n' "$PWD/$h" > "$tmp/t.cpp"
  if out=$(g++ -std=c++20 -fsyntax-only -Wall -Wextra $INC "$tmp/t.cpp" 2>&1); then
    echo "  ok    $h"
  else
    echo "  FAIL  $h  (not self-contained)"
    echo "$out" | grep -E 'error:' | head -3 | sed 's/^/          /'
    fails=$((fails + 1))
  fi
done

echo
if [ "$fails" -gt 0 ]; then
  echo "$fails header(s) rely on includes they do not declare."
  exit 1
fi
echo "all headers are self-contained"
