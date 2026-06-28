#!/usr/bin/env python3
import os
import sys
import subprocess

def main() -> int:
    if len(sys.argv) != 4:
        sys.stderr.write('usage: build_assets.py <assets_dir> <out_dir> <bin_compress_path>\n')
        return 2

    assets_dir = sys.argv[1]
    out_dir = sys.argv[2]
    bin_compress = sys.argv[3]

    os.makedirs(out_dir, exist_ok=True)

    # 1. Run the zx0em_pack compiler
    pack_args = [bin_compress, assets_dir, out_dir, '--stats']

    print(f"Running zx0em_pack: {' '.join(pack_args)}")
    res = subprocess.run(pack_args, capture_output=True)
    if res.returncode != 0:
        sys.stderr.write(f"zx0em_pack failed: {res.stderr.decode('utf-8')}\n")
        return 1

    # 2. Patch zx0em_runtime.h to convert static buffers to extern globals
    zx0em_runtime_h = os.path.join(out_dir, 'zx0em_runtime.h')
    with open(zx0em_runtime_h, 'r', encoding='utf-8') as f:
        run_content = f.read()

    run_content = run_content.replace(
        'static unsigned char zx0em_buf_[ZX0EM_UNCOMPRESSED_SIZE];',
        'extern unsigned char zx0em_buf_[ZX0EM_UNCOMPRESSED_SIZE];'
    )
    run_content = run_content.replace(
        'static int zx0em_inited_ = 0;',
        'extern int zx0em_inited_;'
    )
    run_content = run_content.replace(
        '#if ZX0EM_MAX_PLANAR_SCRATCH_SIZE > 0\nstatic unsigned char _zx0em_planar_scratch[ZX0EM_MAX_PLANAR_SCRATCH_SIZE];\n#endif',
        '#if ZX0EM_MAX_PLANAR_SCRATCH_SIZE > 0\nextern unsigned char _zx0em_planar_scratch[ZX0EM_MAX_PLANAR_SCRATCH_SIZE];\n#else\nextern unsigned char _zx0em_planar_scratch[1];\n#endif'
    )

    with open(zx0em_runtime_h, 'w', encoding='utf-8') as f:
        f.write(run_content)

    out_icons_h = os.path.join(out_dir, 'embedded_icons.h')
    out_images_data_h = os.path.join(out_dir, 'embedded_images_data.h')
    out_images_data_cpp = os.path.join(out_dir, 'embedded_images_data.cpp')
    out_compat_cpp = os.path.join(out_dir, 'embedded_assets_compat.cpp')

    # 4. Generate backwards-compatible embedded_icons.h
    with open(out_icons_h, 'w', encoding='utf-8') as out:
        out.write('// Auto-generated file. Do not edit.\n')
        out.write('#pragma once\n\n')
        out.write('#include "zx0em_runtime.h"\n\n')
        out.write('#define icons_about_png ZX0EM_about_png\n')
        out.write('#define icons_github_png ZX0EM_github_png\n')
        out.write('#define icons_langue_png ZX0EM_langue_png\n')
        out.write('#define icons_konqi_perfs_png ZX0EM_konqi_perfs_png\n')
        out.write('#define icons_tuneperfs_png ZX0EM_tuneperfs_png\n')
        out.write('#define icons_help_png ZX0EM_help_png\n')
        out.write('#define icons_secu_ok_png ZX0EM_secu_ok_png\n')
        out.write('#define icons_home_png ZX0EM_home_png\n')
        out.write('#define icons_role_png ZX0EM_role_png\n')
        out.write('#define icons_profil_png ZX0EM_profil_png\n')
        out.write('#define icons_policies_png ZX0EM_policies_png\n')
        out.write('#define icons_parameters_png ZX0EM_parameters_png\n')
        out.write('#define icons_opti_png ZX0EM_opti_png\n')

    # 5. Generate embedded_images_data.h
    with open(out_images_data_h, 'w', encoding='utf-8') as out:
        out.write('// Auto-generated file. Do not edit.\n')
        out.write('#pragma once\n\n')
        out.write('#include "zx0em_blob_data.h"\n')
        out.write('#include "zx0em_index.h"\n')

    # 6. Generate embedded_images_data.cpp
    with open(out_images_data_cpp, 'w', encoding='utf-8') as out:
        out.write('// Auto-generated file. Do not edit.\n')
        out.write('#include "embedded_images_data.h"\n')
        out.write('#include "zx0em_blob_data.c"\n')

    # 7. Generate embedded_assets_compat.cpp defining zx0em globals
    with open(out_compat_cpp, 'w', encoding='utf-8') as out:
        out.write('// Auto-generated file. Do not edit.\n')
        out.write('#include "zx0em_runtime.h"\n\n')
        out.write('// Define zx0em globals with external linkage\n')
        out.write('unsigned char zx0em_buf_[ZX0EM_UNCOMPRESSED_SIZE];\n')
        out.write('int zx0em_inited_ = 0;\n')
        out.write('#if ZX0EM_MAX_PLANAR_SCRATCH_SIZE > 0\n')
        out.write('unsigned char _zx0em_planar_scratch[ZX0EM_MAX_PLANAR_SCRATCH_SIZE];\n')
        out.write('#else\n')
        out.write('unsigned char _zx0em_planar_scratch[1];\n')
        out.write('#endif\n')

    return 0

if __name__ == '__main__':
    raise SystemExit(main())
