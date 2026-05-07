#!/bin/bash
set -e

AXMOL_VERSION="2.11.3"
AXMOL_URL="https://github.com/axmolengine/axmol/archive/refs/tags/v${AXMOL_VERSION}.tar.gz"
AXMOL_DIR="axmol-${AXMOL_VERSION}"

echo "Downloading Axmol ${AXMOL_VERSION}..."
wget -q --show-progress "$AXMOL_URL" -O axmol.tar.gz

echo "Extracting..."
tar -xzf axmol.tar.gz
mv "$AXMOL_DIR" axmol
rm axmol.tar.gz

# Создаём .gitignore если нет, добавляем axmol/
if [ ! -f .gitignore ]; then
    touch .gitignore
fi

if ! grep -Fxq "axmol/" .gitignore; then
    echo "axmol/" >> .gitignore
    echo "Added axmol/ to .gitignore"
fi

# Дополнительно: игнорируем всё содержимое axmol на всякий случай
if [ -d axmol ]; then
    touch axmol/.gitignore
    echo "*" > axmol/.gitignore
    echo "!.gitignore" >> axmol/.gitignore
fi

git add .gitignore 2>/dev/null || true

echo "Axmol ready at $(pwd)/axmol"