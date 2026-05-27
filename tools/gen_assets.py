#!/usr/bin/env python3
from __future__ import annotations

import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps

ICON_SIZE = 64
# Visible art is rescaled so its longer side matches ICON_INNER_SIZE,
# then centered in the ICON_SIZE×ICON_SIZE canvas. Faint soft-edge
# pixels (alpha below ICON_ALPHA_THRESHOLD) are ignored when computing
# the art bounding box so a soft halo doesn't shrink the apparent size.
ICON_INNER_SIZE = 60
ICON_ALPHA_THRESHOLD = 12
# El modo VBE de Ridux arranca en 1024x768 en VirtualBox; guardar el wallpaper
# a ese tamano evita que el desktop lo estire y se vea lavado/pixelado.
WALLPAPER_W = 1024
WALLPAPER_H = 768


@dataclass(frozen=True)
class AssetSpec:
    key: str
    candidates: tuple[str, ...]


ICON_SPECS: tuple[AssetSpec, ...] = (
    AssetSpec("START", ("RiduxIcons/RiduxIconLogo.png", "RiduxIcons/SeageLogoIcon.png")),
    AssetSpec("MONITOR", ("RiduxIcons/AdministradorTareasIcon.png",)),
    AssetSpec("FILES", ("RiduxIcons/FileBrowserIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("TERMINAL", ("RiduxIcons/TerminalIcon.png",)),
    AssetSpec("FLUSH", ("RiduxIcons/CloudIcon.png", "RiduxIcons/CalculatorIcon.png")),
    AssetSpec("SETTINGS", ("RiduxIcons/SettingsIcon.png",)),
    AssetSpec("NOTES", ("RiduxIcons/NotesIcon.png", "RiduxIcons/DocumentsFolderIcon.png")),
    AssetSpec("CALC", ("RiduxIcons/CalculatorIcon.png",)),
    AssetSpec("CLOCK", ("RiduxIcons/ClockIcon.png",)),
    AssetSpec("PAINT", ("RiduxIcons/ImageIcon.png", "RiduxIcons/CameraIcon.png")),
    AssetSpec("TASKMGR", ("RiduxIcons/AdministradorTareasIcon.png",)),
    AssetSpec("BROWSER", ("RiduxIcons/SearchLupaIcon.png", "RiduxIcons/SeageLogoIcon.png")),
    AssetSpec("FIREFOX", ("RiduxIcons/SeageLogoIcon.png", "RiduxIcons/SearchLupaIcon.png")),
    AssetSpec("WEATHER", ("RiduxIcons/ClimaIcon.png", "RiduxIcons/CloudIcon.png")),
    AssetSpec("STORE", ("RiduxIcons/RiduxStore.png",)),
    AssetSpec("ABOUT", ("RiduxIcons/RiduxIconLogo.png",)),
    AssetSpec("MEDIA", ("RiduxIcons/ReproductorMusicaIcon.png", "RiduxIcons/MusicFolderIcon.png")),
    AssetSpec("EDITOR", ("RiduxIcons/DocumentsFolderIcon.png", "RiduxIcons/NotesIcon.png")),
    AssetSpec("MINESWEEPER", ("RiduxIcons/NoticesIcon.png", "RiduxIcons/CalendarIcon.png")),
    AssetSpec("SNAKE", ("RiduxIcons/PixieIcon.png", "RiduxIcons/ChairIcon.png")),
    AssetSpec("LOGVIEWER", ("RiduxIcons/NotificationsIcons.png", "RiduxIcons/NoticesIcon.png")),
    AssetSpec("NETWORK", ("RiduxIcons/WifiIcon.png", "RiduxIcons/BluetoothIcon.png")),
    AssetSpec("PROCESSES", ("RiduxIcons/AdministradorTareasIcon.png",)),
    AssetSpec("SYSINFO", ("RiduxIcons/MiPCIcon.png", "RiduxIcons/MapsIcon.png")),
    AssetSpec("TICTACTOE", ("RiduxIcons/CalendarIcon.png", "RiduxIcons/NotesIcon.png")),
    AssetSpec("TRAY_NETWORK", ("RiduxIcons/Fluent/tray_network.png", "RiduxIcons/WifiIcon.png")),
    AssetSpec("TRAY_VOLUME", ("RiduxIcons/Fluent/tray_volume.png", "RiduxIcons/VoiceRecorderIcon.png")),
    AssetSpec("TRAY_BATTERY", ("RiduxIcons/Fluent/tray_battery.png", "RiduxIcons/SettingsIcon.png")),
    AssetSpec("TRAY_SETTINGS", ("RiduxIcons/Fluent/tray_settings.png", "RiduxIcons/SettingsIcon.png")),
    AssetSpec("FOLDER_HOME", ("RiduxIcons/HomeFolderIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_DOCUMENTS", ("RiduxIcons/DocumentsFolderIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_DOWNLOADS", ("RiduxIcons/DownloadsFolder.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_MUSIC", ("RiduxIcons/MusicFolderIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_PICTURES", ("RiduxIcons/ImageFolderIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_VIDEOS", ("RiduxIcons/VideoFolderIcon.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FOLDER_DESKTOP", ("RiduxIcons/FolderDekstop.png", "RiduxIcons/FolderIcon.png")),
    AssetSpec("FILE_PDF", ("RiduxIcons/PDFFileIcon.png", "RiduxIcons/SimpleFileIcon.png")),
    AssetSpec("FILE_IMAGE", ("RiduxIcons/ImageFileIcon.png", "RiduxIcons/SimpleFileIcon.png")),
    AssetSpec("FILE_TEXT", ("RiduxIcons/txtFileIcon.png", "RiduxIcons/SimpleFileIcon.png")),
    AssetSpec("PERSONALIZATION", ("RiduxIcons/PersonalizationIcon.png", "RiduxIcons/SettingsIcon.png")),
    AssetSpec("USER", ("RiduxIcons/UserIcon.png", "RiduxIcons/RiduxIconLogo.png")),
)

WALLPAPER_SPECS: tuple[AssetSpec, ...] = (
    AssetSpec("A", ("RiduxIcons/Wallpaper/DefaultWallpaper.png", "Wallpapers/1.jpg")),
    AssetSpec("B", ("Wallpapers/2.jpg", "Wallpapers/4.jpg")),
    AssetSpec("C", ("Wallpapers/8.jpg", "Wallpapers/10.jpg")),
)


def pick_existing(root: Path, candidates: tuple[str, ...]) -> Path | None:
    for rel in candidates:
        path = root / rel
        if path.exists():
            return path
    return None


def make_placeholder(size: tuple[int, int], label: str, transparent: bool) -> Image.Image:
    base_alpha = 0 if transparent else 255
    image = Image.new("RGBA", size, (25, 43, 69, base_alpha))
    draw = ImageDraw.Draw(image, "RGBA")
    w, h = size
    draw.rounded_rectangle((0, 0, w - 1, h - 1), radius=max(2, w // 6), fill=(39, 68, 107, 235), outline=(169, 213, 255, 220), width=2)
    if label:
        txt = label[0].upper()
        tx = (w - 8) // 2
        ty = (h - 11) // 2
        draw.text((tx, ty), txt, fill=(240, 247, 255, 255))
    return image


def alpha_bbox(image: Image.Image, threshold: int) -> tuple[int, int, int, int] | None:
    """Bounding box of pixels whose alpha >= threshold.

    Falls back to the unfiltered alpha bbox if thresholding strips the
    whole image (e.g. a fully translucent stylised glyph)."""
    alpha = image.getchannel("A")
    mask = alpha.point(lambda v: 255 if v >= threshold else 0)
    box = mask.getbbox()
    if box is None:
        box = alpha.getbbox()
    return box


def prepare_icon(path: Path | None, label: str) -> Image.Image:
    if path is None:
        return make_placeholder((ICON_SIZE, ICON_SIZE), label, True)
    source = Image.open(path).convert("RGBA")
    box = alpha_bbox(source, ICON_ALPHA_THRESHOLD)
    if box is not None:
        source = source.crop(box)
    # Rescale so the longer side of the visible art exactly hits
    # ICON_INNER_SIZE; this normalises the apparent size of the glyph
    # across icons with very different aspect ratios.
    target = ICON_INNER_SIZE
    source = ImageOps.contain(source, (target, target), method=Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
    x = (ICON_SIZE - source.width) // 2
    y = (ICON_SIZE - source.height) // 2
    canvas.paste(source, (x, y), source)
    return canvas


def prepare_wallpaper(path: Path | None, label: str) -> Image.Image:
    if path is None:
        return make_placeholder((WALLPAPER_W, WALLPAPER_H), label, False)
    source = Image.open(path).convert("RGB")
    fitted = ImageOps.fit(source, (WALLPAPER_W, WALLPAPER_H), method=Image.Resampling.LANCZOS, centering=(0.5, 0.5))
    return fitted.convert("RGBA")


def rgba_to_u32(image: Image.Image) -> list[int]:
    pixels: list[int] = []
    for r, g, b, a in image.getdata():
        pixels.append((a << 24) | (r << 16) | (g << 8) | b)
    return pixels


def write_array(f, name: str, values: list[int], per_line: int = 10) -> None:
    f.write(f"static const uint32_t {name}[{len(values)}] = {{\n")
    for i in range(0, len(values), per_line):
        chunk = values[i : i + per_line]
        text = ", ".join(f"0x{v:08X}" for v in chunk)
        f.write(f"    {text},\n")
    f.write("};\n\n")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gen_assets.py <output_header>", file=sys.stderr)
        return 1

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)

    repo_root = Path(__file__).resolve().parents[1]

    icon_images: list[tuple[str, Image.Image]] = []
    for spec in ICON_SPECS:
        picked = pick_existing(repo_root, spec.candidates)
        if picked is None:
            print(f"[assets] icon {spec.key}: fallback placeholder", file=sys.stderr)
        else:
            print(f"[assets] icon {spec.key}: {picked.relative_to(repo_root)}", file=sys.stderr)
        icon_images.append((spec.key, prepare_icon(picked, spec.key)))

    wallpaper_images: list[tuple[str, Image.Image]] = []
    for spec in WALLPAPER_SPECS:
        picked = pick_existing(repo_root, spec.candidates)
        if picked is None:
            print(f"[assets] wallpaper {spec.key}: fallback placeholder", file=sys.stderr)
        else:
            print(f"[assets] wallpaper {spec.key}: {picked.relative_to(repo_root)}", file=sys.stderr)
        wallpaper_images.append((spec.key, prepare_wallpaper(picked, spec.key)))

    with output.open("w", encoding="ascii", newline="\n") as f:
        f.write("#ifndef RIDUX_ASSETS_H\n")
        f.write("#define RIDUX_ASSETS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t width;\n")
        f.write("    uint16_t height;\n")
        f.write("    const uint32_t *pixels; /* AARRGGBB */\n")
        f.write("} ridux_image_t;\n\n")

        for key, image in icon_images:
            write_array(f, f"RIDUX_ICON_{key}_PIXELS", rgba_to_u32(image))
        for key, image in wallpaper_images:
            write_array(f, f"RIDUX_WALLPAPER_{key}_PIXELS", rgba_to_u32(image))

        f.write("enum {\n")
        for i, (key, _) in enumerate(icon_images):
            f.write(f"    RIDUX_ICON_{key} = {i},\n")
        f.write(f"    RIDUX_ICON_COUNT = {len(icon_images)}\n")
        f.write("};\n\n")

        f.write("enum {\n")
        for i, (key, _) in enumerate(wallpaper_images):
            f.write(f"    RIDUX_WALLPAPER_{key} = {i},\n")
        f.write(f"    RIDUX_WALLPAPER_COUNT = {len(wallpaper_images)}\n")
        f.write("};\n\n")

        f.write("static const ridux_image_t RIDUX_ICONS[RIDUX_ICON_COUNT] = {\n")
        for key, _ in icon_images:
            f.write(
                f"    {{ {ICON_SIZE}, {ICON_SIZE}, RIDUX_ICON_{key}_PIXELS }},\n"
            )
        f.write("};\n\n")

        f.write("static const ridux_image_t RIDUX_WALLPAPERS[RIDUX_WALLPAPER_COUNT] = {\n")
        for key, _ in wallpaper_images:
            f.write(
                f"    {{ {WALLPAPER_W}, {WALLPAPER_H}, RIDUX_WALLPAPER_{key}_PIXELS }},\n"
            )
        f.write("};\n\n")

        f.write("#endif\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
