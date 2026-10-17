#!/bin/sh
# compile.sh: the Vulkan renderer's shaders into the SPIR-V arrays the
# core carries, neo/renderer/vk/spirv/*.h, one per program variant.  Run
# after changing anything under neo/renderer/vk/shaders/ and commit the
# output; the core itself links nothing of the shader toolchain.
#
# Needs glslangValidator (glslang-tools) and spirv-val (spirv-tools) on
# the host.
#
#   ./compile.sh            from tools/vk_shaders
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../neo/renderer/vk/shaders"
out="$here/../../neo/renderer/vk/spirv"
mkdir -p "$out"
tmp=$(mktemp -d)

# name  source  defines
variants="
zfill_vert zfill.vert
zfill_opaque_frag zfill.frag -DALPHA_TEST=0 -DCLIP=0
zfill_alpha_frag zfill.frag -DALPHA_TEST=1 -DCLIP=0
zfill_clip_frag zfill.frag -DALPHA_TEST=1 -DCLIP=1
diffuse_vert diffuse.vert
diffuse_frag diffuse.frag -DGUI=0
diffuse_gui_frag diffuse.frag -DGUI=1
skybox_vert cube.vert -DMODE=0
reflect_vert cube.vert -DMODE=1
diffuse_cube_vert cube.vert -DMODE=2
cube_frag cube.frag
blit_vert blit.vert
blit_frag blit.frag
interaction_vert interaction.vert -DPHONG=0
interaction_frag interaction.frag -DPHONG=0
interaction_phong_vert interaction.vert -DPHONG=1
interaction_phong_frag interaction.frag -DPHONG=1
shadow_vert shadow.vert
shadow_frag shadow.frag
fog_vert fog.vert -DBLEND=0
blendlight_vert fog.vert -DBLEND=1
fog_frag fog.frag
softparticle_frag softparticle.frag
heathaze_vert heathaze.vert
heathaze_frag heathaze.frag -DMASK=0
heathaze_mask_frag heathaze.frag -DMASK=1
heathaze_mask_vertex_frag heathaze.frag -DMASK=2
colorprocess_vert colorprocess.vert
colorprocess_frag colorprocess.frag
bumpyenv_vert bumpyenv.vert
bumpyenv_frag bumpyenv.frag
"
echo "$variants" | while read -r name file defines; do
	[ -z "$name" ] && continue
	glslangValidator -V --target-env vulkan1.1 -I"$src" $defines -o "$tmp/$name.spv" "$src/$file" > "$tmp/log" 2>&1 \
		|| { cat "$tmp/log"; exit 1; }
	spirv-val --target-env vulkan1.1 "$tmp/$name.spv"
	{
		echo "/* $file $defines - made by tools/vk_shaders/compile.sh, do not edit */"
		echo "static const unsigned int spirv_${name}[] = {"
		od -An -tx4 -v -w32 "$tmp/$name.spv" | sed 's/ \([0-9a-f]*\)/ 0x\1,/g'
		echo "};"
	} > "$out/$name.h"
	echo "$name: $(stat -c %s "$tmp/$name.spv") bytes"
done
rm -rf "$tmp"
