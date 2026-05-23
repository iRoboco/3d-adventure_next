#!/bin/bash
set -e

AXMOL_VERSION="2.11.3"
AXMOL_URL="https://github.com/axmolengine/axmol/archive/refs/tags/v${AXMOL_VERSION}.tar.gz"
TMP_DIR="/tmp/axmol-download-$$"

echo "Downloading Axmol ${AXMOL_VERSION} headers..."

mkdir -p "$TMP_DIR"
cd "$TMP_DIR"

wget -q --show-progress "$AXMOL_URL" -O axmol.tar.gz
tar -xzf axmol.tar.gz --wildcards "axmol-${AXMOL_VERSION}/core/**.h" "axmol-${AXMOL_VERSION}/core/**.hpp" "axmol-${AXMOL_VERSION}/extensions/**.h" "axmol-${AXMOL_VERSION}/extensions/**.hpp" 2>/dev/null || true

# Копируем только заголовки в проект
mkdir -p "$OLDPWD/axmol/include"
cp -r "axmol-${AXMOL_VERSION}/core" "$OLDPWD/axmol/include/"
cp -r "axmol-${AXMOL_VERSION}/extensions" "$OLDPWD/axmol/include/" 2>/dev/null || true

cd "$OLDPWD"
rm -rf "$TMP_DIR"

# Добавляем в .gitignore
if [ ! -f .gitignore ]; then
    touch .gitignore
fi
if ! grep -Fxq "axmol/" .gitignore; then
    echo "axmol/" >> .gitignore
fi

# Дополнительно: игнорируем всё содержимое axmol на всякий случай
if [ -d axmol ]; then
    touch axmol/.gitignore
    echo "*" > axmol/.gitignore
    echo "!.gitignore" >> axmol/.gitignore
fi

git add .gitignore 2>/dev/null || true

echo "Axmol headers ready at $(pwd)/axmol/include"