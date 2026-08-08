from pathlib import Path
p = Path('MaterialArtifact.cppm')
s = p.read_text()
old_slots = '        friend bool operator==(const MaterialTextureSlots&, const MaterialTextureSlots&) = default;'
new_slots = '''        friend bool operator==(const MaterialTextureSlots& left, const MaterialTextureSlots& right) noexcept
        {
            const auto same = [](const std::optional<TextureAssetHandle>& a,
                const std::optional<TextureAssetHandle>& b) noexcept
            {
                if (a.has_value() != b.has_value()) return false;
                return !a.has_value() || a->ID == b->ID;
            };
            return same(left.BaseColor, right.BaseColor) &&
                same(left.Normal, right.Normal) &&
                same(left.MetallicRoughness, right.MetallicRoughness) &&
                same(left.Emissive, right.Emissive) &&
                same(left.Occlusion, right.Occlusion);
        }'''
old_data = '        friend bool operator==(const MaterialArtifactData&, const MaterialArtifactData&) = default;'
new_data = '''        friend bool operator==(const MaterialArtifactData& left, const MaterialArtifactData& right) noexcept
        {
            for (std::size_t index = 0u; index < left.BaseColorFactor.size(); ++index)
                if (left.BaseColorFactor[index] != right.BaseColorFactor[index]) return false;
            for (std::size_t index = 0u; index < left.EmissiveFactor.size(); ++index)
                if (left.EmissiveFactor[index] != right.EmissiveFactor[index]) return false;
            return left.MetallicFactor == right.MetallicFactor &&
                left.RoughnessFactor == right.RoughnessFactor &&
                left.NormalScale == right.NormalScale &&
                left.OcclusionStrength == right.OcclusionStrength &&
                left.AlphaMode == right.AlphaMode &&
                left.AlphaCutoff == right.AlphaCutoff &&
                left.DoubleSided == right.DoubleSided &&
                left.Textures == right.Textures;
        }'''
if old_slots not in s or old_data not in s:
    raise SystemExit('material equality patterns not found')
s = s.replace(old_slots, new_slots).replace(old_data, new_data)
p.write_text(s)
Path('.github/workflows/fix-material-equality.yml').unlink()
Path('.github/scripts/fix_material_equality.py').unlink()
