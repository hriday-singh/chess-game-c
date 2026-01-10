import re
import sys

def fix_padding(filepath):
    print(f"Adjusting padding for {filepath}...")
    try:
        with open(filepath, 'r') as f:
            content = f.read()
    except FileNotFoundError:
        print(f"Error: File {filepath} not found.")
        return
    
    # SAFE CROP: 
    # Original: 0 0 512 512
    # Previous (Clipped): 60 20 400 460
    # New (Safe): 35 15 442 482 (Less aggressive crop)
    # We leave more room on sides (35 instead of 60) and top (15 instead of 20)
    # Width: 512 - 35 - 35 = 442
    # Height: 512 - 15 - 15 = 482
    
    new_viewbox = 'viewBox="35 15 442 482"'
    
    new_content = re.sub(r'viewBox="[^"]+"', new_viewbox, content)
    
    with open(filepath, 'w') as f:
        f.write(new_content)
    print(f"Done. viewBox updated to '{new_viewbox}'.")

if __name__ == "__main__":
    fix_padding("assets/images/icon/icon.svg")
