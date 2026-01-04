import os
import re

SRC_DIR = "src"


def bytes_to_cpp_byte_array(input_file_bytes: bytes, input_file_path: str, output_header_file: str, variable_name: str):
    # Format bytes into C++ byte array initialiser format (hex values)
    hex_values = [f"0x{b:02x}" for b in input_file_bytes]
    bytes_array_declaration = ", ".join(hex_values)
    data_length = len(input_file_bytes)

    # Generate the C++ header file content
    header_content = f"""
#ifndef {variable_name.upper()}_H
#define {variable_name.upper()}_H

#include <cstddef>
#include <cstdint>

// Embedded file: {os.path.basename(input_file_path)}
constexpr uint8_t {variable_name}_data[] = {{
    {bytes_array_declaration}
}};
constexpr size_t {variable_name}_size = {data_length};

#endif // {variable_name.upper()}_H
"""

    try:
        with open(output_header_file, 'w') as f:
            f.write(header_content)
        print(f"Successfully generated C++ header file: {output_header_file}")
    except IOError as e:
        print(f"Error writing header file: {e}")
        exit(1)

def minify_html(html: str) -> str:
    # Tags where whitespace should be preserved
    preserve_tags = ['pre', 'code', 'textarea', 'script', 'style']
    preserve_regex = '|'.join(preserve_tags)

    # Protect preserve blocks with placeholders
    preserve_blocks = []
    def preserve(match):
        preserve_blocks.append(match.group(0))
        return f"__PRESERVE_BLOCK_{len(preserve_blocks)-1}__"

    html = re.sub(rf'<({preserve_regex})[\s\S]*?</\1>', preserve, html, flags=re.IGNORECASE)

    # Remove HTML comments
    html = re.sub(r'<!--.*?-->', '', html, flags=re.DOTALL)

    # Collapse all whitespace between tags
    html = re.sub(r'>\s+<', '><', html)

    # Collapse multiple spaces inside tags
    html = re.sub(r'\s+', ' ', html)

    # Restore preserved blocks
    for i, block in enumerate(preserve_blocks):
        html = html.replace(f"__PRESERVE_BLOCK_{i}__", block)

    return html.strip()

def read_src_file(root: str, file: str) -> str:
    file_path = os.path.join(root, file)
    with open(file_path, "r", encoding="utf-8") as f:
        return f.read()

def build_static(src_path: str, dest_dir: str):
    filename = os.path.basename(src_path)
    base_name, extension = os.path.splitext(filename)
    postfix = extension[1:].capitalize()
    base_name = f"{os.path.splitext(filename)[0]}{postfix}"
    output_cpp_header = os.path.join(dest_dir, f"{base_name}.generated.h")
    cpp_variable_name = base_name
    if not os.path.exists(dest_dir):
        os.makedirs(dest_dir)
    
    if extension == ".html":
        # minify HTML content
        src_string = open(src_path, "r").read()
        minified_str = minify_html(src_string)
        src_bytes = minified_str.encode("utf-8")
    else:
        src_bytes = open(src_path, "rb").read()

    bytes_to_cpp_byte_array(
        input_file_bytes=src_bytes,
        input_file_path=src_path,
        output_header_file=output_cpp_header,
        variable_name=cpp_variable_name
    )

for root, _, files in os.walk(SRC_DIR):
    for file in files:
        if file.endswith((".html", ".css", ".js")):
            build_static(os.path.join(root, file), root)
