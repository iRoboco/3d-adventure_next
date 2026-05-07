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

# Добавляем axmol в .gitignore если ещё не добавлен
if ! grep -q "^axmol/$" .gitignore 2>/dev/null; then
    echo "Adding axmol/ to .gitignore..."
    echo "axmol/" >> .gitignore
fi

echo "Axmol ready at $(pwd)/axmol"