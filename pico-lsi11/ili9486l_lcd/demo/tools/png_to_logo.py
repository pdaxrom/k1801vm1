#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
  from PIL import Image
except ImportError:
  print("Pillow is required. Install it with: python3 -m pip install Pillow", file=sys.stderr)
  sys.exit(2)


PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_OUTPUT = PROJECT_ROOT / "demo" / "assets" / "logo.jpg"


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
    description="Normalize a PNG or JPEG into the baseline JPEG boot logo used by the ILI9486L demo."
  )
  parser.add_argument("input", type=Path, help="Source image path")
  parser.add_argument("--width", type=int, default=480, help="Output width in pixels")
  parser.add_argument("--height", type=int, default=320, help="Output height in pixels")
  parser.add_argument(
    "--rotate",
    choices=("none", "cw", "ccw", "180"),
    default="none",
    help="Rotate image before resizing",
  )
  parser.add_argument(
    "--output",
    type=Path,
    default=DEFAULT_OUTPUT,
    help="Destination baseline JPEG asset path",
  )
  parser.add_argument("--quality", type=int, default=90, help="JPEG quality (1..95)")
  return parser.parse_args()


def resampling_filter() -> int:
  if hasattr(Image, "Resampling"):
    return Image.Resampling.LANCZOS
  return Image.LANCZOS


def rotation_mode(name: str):
  if name == "cw":
    return Image.Transpose.ROTATE_270
  if name == "ccw":
    return Image.Transpose.ROTATE_90
  if name == "180":
    return Image.Transpose.ROTATE_180
  return None


def load_rgb_image(path: Path, rotate: str, width: int, height: int) -> Image.Image:
  image = Image.open(path)

  if image.mode in ("RGBA", "LA") or "transparency" in image.info:
    rgba = image.convert("RGBA")
    background = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    image = Image.alpha_composite(background, rgba)

  image = image.convert("RGB")

  transpose = rotation_mode(rotate)
  if transpose is not None:
    image = image.transpose(transpose)

  return image.resize((width, height), resample=resampling_filter())

def write_logo_jpeg(path: Path, image: Image.Image, quality: int) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  image.save(path, format="JPEG", quality=max(1, min(95, quality)), optimize=True, progressive=False)


def main() -> int:
  args = parse_args()
  image = load_rgb_image(args.input, args.rotate, args.width, args.height)
  write_logo_jpeg(args.output, image, args.quality)
  print(f"Wrote baseline JPEG logo {image.width}x{image.height} to {args.output}")

  return 0


if __name__ == "__main__":
  sys.exit(main())
