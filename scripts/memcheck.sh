#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 <command> [args...]"
  exit 2
fi

if command -v valgrind >/dev/null 2>&1; then
  exec valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes "$@"
fi

if command -v leaks >/dev/null 2>&1; then
  exec leaks --atExit -- "$@"
fi

echo "No valgrind or leaks available on PATH."
exit 1
