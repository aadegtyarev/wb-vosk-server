#!/bin/bash

FQBN="m5stack:esp32:m5stack_atom"  # Фиксированная плата

# Определяем путь к папке скетча (там, где лежит скрипт)
SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Находим .ino-файл (если их несколько — берём первый)
SKETCH_FILE=$(find "$SKETCH_DIR" -maxdepth 1 -name "*.ino" | head -n 1)

if [[ -z "$SKETCH_FILE" ]]; then
  echo "Ошибка: .ino-файл не найден в $SKETCH_DIR!"
  exit 1
fi

# Если нет аргументов — просто компилируем
if [[ $# -eq 0 ]]; then
  echo "Компиляция скетча $SKETCH_FILE для платы $FQBN..."
  arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"
else
  # Передаём все аргументы, включая FQBN
  echo "Запуск: arduino-cli compile --fqbn $FQBN $* $SKETCH_DIR"
  arduino-cli compile --fqbn "$FQBN" "$@" "$SKETCH_DIR"
fi
