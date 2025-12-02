import os

def embed_file():
    html_path = os.path.join("frontend", "dist", "index.html")
    header_path = os.path.join("Source", "FrontendAssets.h")

    if not os.path.exists(html_path):
        print(f"Error: {html_path} does not exist. Run 'npm run build' first.")
        return

    with open(html_path, "rb") as f:
        content = f.read()

    # Create C++ array
    hex_content = ", ".join([f"0x{b:02x}" for b in content])

    header_content = f"""#pragma once
#include <cstddef>
#include <vector>

namespace FrontendAssets
{{
    const unsigned char index_html_data[] = {{ {hex_content} }};
    const size_t index_html_size = {len(content)};
}}
"""

    with open(header_path, "w") as f:
        f.write(header_content)

    print(f"Successfully generated {header_path}")

if __name__ == "__main__":
    embed_file()
