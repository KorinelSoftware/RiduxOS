#!/usr/bin/env bash
set -euo pipefail

KDE_DIR="${1:-third_party/kde}"
SRC_DIR="$KDE_DIR/src"
BUILD_DIR="$KDE_DIR/build"
INSTALL_DIR="$KDE_DIR/install"
CONFIG="$KDE_DIR/kde-builder.yaml"
MANIFEST="$KDE_DIR/source-manifest.txt"

mkdir -p "$SRC_DIR" "$BUILD_DIR" "$INSTALL_DIR"

cat > "$CONFIG" <<EOF
config-version: 2

global:
  branch-group: stable-kf6
  source-dir: $SRC_DIR
  build-dir: $BUILD_DIR
  install-dir: $INSTALL_DIR
  cmake-options: -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=OFF
  include-dependencies: true

project qt6-set:
  repository: qt-project
  branch-group: qt6-set

project-set workspace:
  repository: kde-projects
  branch-group: stable-kf6
  use-projects:
    - frameworks/extra-cmake-modules
    - frameworks/kconfig
    - frameworks/kcoreaddons
    - frameworks/ki18n
    - frameworks/kio
    - frameworks/kpackage
    - frameworks/kservice
    - frameworks/kwayland
    - frameworks/kwindowsystem
    - plasma/kwayland-server
    - plasma/kwin
    - plasma/plasma-workspace
    - plasma/plasma-desktop
    - plasma/systemsettings
EOF

cat > "$MANIFEST" <<EOF
RiduxOS KDE Plasma source workspace
config=$CONFIG
source_dir=$SRC_DIR
build_dir=$BUILD_DIR
install_dir=$INSTALL_DIR
branch_group=stable-kf6
qt_stage=qt6-set
workspace_stage=workspace
created_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
EOF

echo "[kde-source] workspace prepared"
echo "  config: $CONFIG"
echo "  source: $SRC_DIR"
echo "  build:  $BUILD_DIR"
echo "  install:$INSTALL_DIR"
echo
echo "[kde-source] Next:"
echo "  make kde-build"
echo "  RIDUX_KDE_REAL_BUILD=1 make kde-build   # long Qt/KF/Plasma source build"
