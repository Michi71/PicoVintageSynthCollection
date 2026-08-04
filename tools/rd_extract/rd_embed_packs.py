import sys
import os

def main():
    if len(sys.argv) != 4:
        print("Usage: rd_embed_packs.py <in_dir> <out_cpp> <out_h>", file=sys.stderr)
        sys.exit(1)

    in_dir = sys.argv[1]
    out_cpp = sys.argv[2]
    out_h = sys.argv[3]
    h_basename = os.path.basename(out_h)

    h_content = f"""#ifndef RD_PACKS_DATA_H
#define RD_PACKS_DATA_H

#include <stdint.h>
#include <stddef.h>

extern const uint8_t* const rd_pack_ptrs[16];
extern const size_t rd_pack_sizes[16];

#endif // RD_PACKS_DATA_H
"""
    with open(out_h, "w") as f:
        f.write(h_content)

    cpp_lines = [f'#include "{h_basename}"', ""]
    sizes = []
    total_size = 0

    for i in range(16):
        fname = os.path.join(in_dir, f"pack_p{i}.rdp")
        if not os.path.exists(fname):
            print(f"Error: Missing file {fname}", file=sys.stderr)
            sys.exit(1)

        with open(fname, "rb") as f:
            data = f.read()

        sizes.append(len(data))
        total_size += len(data)

        cpp_lines.append(f"static const uint8_t s_pack_p{i}[] __attribute__((aligned(4))) = {{")
        if len(data) == 0:
            cpp_lines.append("    0")
        else:
            for j in range(0, len(data), 16):
                chunk = data[j:j+16]
                hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
                cpp_lines.append(f"    {hex_str},")
        cpp_lines.append("};")
        cpp_lines.append("")

    cpp_lines.append("const uint8_t* const rd_pack_ptrs[16] = {")
    for i in range(16):
        cpp_lines.append(f"    s_pack_p{i},")
    cpp_lines.append("};")
    cpp_lines.append("")

    cpp_lines.append("const size_t rd_pack_sizes[16] = {")
    for i in range(16):
        cpp_lines.append(f"    {sizes[i]},")
    cpp_lines.append("};")
    cpp_lines.append("")

    with open(out_cpp, "w") as f:
        f.write("\n".join(cpp_lines))

    print(total_size)

if __name__ == "__main__":
    main()
