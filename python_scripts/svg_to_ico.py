import sys
import os
import subprocess
from io import BytesIO

# Try importing Pillow
try:
    from PIL import Image
except ImportError:
    print("Error: 'Pillow' module not found.")
    print("Please install it: pip install Pillow")
    sys.exit(1)

# Try importing cairosvg, but allowing fallback if missing or if DLLs are broken
try:
    import cairosvg
    HAS_CAIROSVG = True
except (ImportError, OSError):
    HAS_CAIROSVG = False

def rasterize_svg(svg_path, size):
    """
    Rasterize SVG to a PIL Image of the given size.
    Tries cairosvg first, then rsvg-convert (CLI).
    """
    # Method 1: cairosvg (Best quality, requires libraries)
    if HAS_CAIROSVG:
        try:
            png_data = cairosvg.svg2png(url=svg_path, output_width=size, output_height=size)
            return Image.open(BytesIO(png_data)).convert("RGBA")
        except Exception as e:
            print(f"Warning: cairosvg failed for size {size}: {e}")
    
    # Method 2: rsvg-convert (CLI tool, common on Linux/MinGW)
    # Check if rsvg-convert is in path
    try:
        # Run rsvg-convert to stdout
        result = subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size), svg_path],
            capture_output=True,
            check=True
        )
        return Image.open(BytesIO(result.stdout)).convert("RGBA")
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass

    return None

def convert_svg_to_ico(svg_path, output_path):
    if not os.path.exists(svg_path):
        print(f"Error: File not found: {svg_path}")
        return

    # Standard Windows ICO sizes
    sizes = [256, 128, 64, 48, 32, 16]
    img_layers = []

    print(f"Converting '{svg_path}' to '{output_path}'...")
    
    if not HAS_CAIROSVG:
        print("Note: 'cairosvg' not found. Attempting to use 'rsvg-convert' CLI...")

    for size in sizes:
        img = rasterize_svg(svg_path, size)
        if img:
            img_layers.append(img)
            print(f"  - Generated layer: {size}x{size}")
        else:
            print(f"  - Failed to generate layer: {size}x{size}")

    if not img_layers:
        print("Error: Could not generate any layers. Please install 'cairosvg' (pip install cairosvg) or ensure 'rsvg-convert' is in your PATH.")
        return

    # Save as ICO with all layers
    # Use the first image (largest) as the "base" and append the rest
    # Using format='ICO' automatically handles the sizes if passed as distinct images in 'append_images'
    try:
        img_layers[0].save(output_path, format='ICO', append_images=img_layers[1:])
        print("-" * 30)
        print(f"Success! Saved multi-layer ICO to: {output_path}")
        print("You can verify the layers using 'analyze_ico.py'.")
    except Exception as e:
        print(f"Error saving ICO: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python svg_to_ico.py <input.svg> [output.ico]")
        print("Example: python svg_to_ico.py assets/images/logo.svg assets/images/icon/icon.ico")
    else:
        svg_in = sys.argv[1]
        ico_out = sys.argv[2] if len(sys.argv) > 2 else "icon.ico"
        convert_svg_to_ico(svg_in, ico_out)
