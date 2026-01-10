import sys
import os
import subprocess
from io import BytesIO

# Try importing Pillow
try:
    from PIL import Image
    HAS_PILLOW = True
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
            pass # Silently fail to try next method
    
    # Method 2: rsvg-convert (CLI tool, common on Linux/MinGW)
    try:
        result = subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size), svg_path],
            capture_output=True,
            check=True
        )
        return Image.open(BytesIO(result.stdout)).convert("RGBA")
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass

    return None

def process_raster_image(image_path, size):
    """
    Open a raster image (PNG, JPG) and resize it to the given size using high-quality resampling.
    """
    try:
        with Image.open(image_path) as img:
            # Convert to RGBA to ensure transparency support
            img = img.convert("RGBA")
            # Log resizing action
            if size == 256: # Only print once or for the largest layer to avoid spam, or just let the main loop print
                 pass 
            # Resize using LANCZOS for best downscaling quality
            # If the image is 1024x1024, this downscales it to size x size
            return img.resize((size, size), Image.Resampling.LANCZOS)
    except Exception as e:
        print(f"Error processing raster image for size {size}: {e}")
        return None

def convert_to_ico(input_path, output_path):
    if not os.path.exists(input_path):
        print(f"Error: File not found: {input_path}")
        return

    # Determine file type
    ext = os.path.splitext(input_path)[1].lower()
    is_svg = (ext == ".svg")

    if is_svg:
        print(f"Converting SVG '{input_path}' to ICO...")
        if not HAS_CAIROSVG:
            print("Note: 'cairosvg' not found. Attempting to use 'rsvg-convert' CLI...")
    else:
        print(f"Converting Raster Image ({ext}) '{input_path}' to ICO...")

    # Standard Windows ICO sizes
    sizes = [256, 128, 64, 48, 32, 16]
    img_layers = []

    for size in sizes:
        if is_svg:
            img = rasterize_svg(input_path, size)
        else:
            img = process_raster_image(input_path, size)
            
        if img:
            img_layers.append(img)
            print(f"  - Generated layer: {size}x{size}")
        else:
            print(f"  - Failed to generate layer: {size}x{size}")

    if not img_layers:
        print("Error: Could not generate any layers.")
        return

    # Save as ICO with all layers
    try:
        img_layers[0].save(output_path, format='ICO', append_images=img_layers[1:])
        print("-" * 30)
        print(f"Success! Saved multi-layer ICO to: {output_path}")
    except Exception as e:
        print(f"Error saving ICO: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python image_to_ico.py <input_file> [output.ico]")
        print("Supported formats: SVG, PNG, JPG, JPEG")
    else:
        input_file = sys.argv[1]
        output_file = sys.argv[2] if len(sys.argv) > 2 else "icon.ico"
        convert_to_ico(input_file, output_file)
