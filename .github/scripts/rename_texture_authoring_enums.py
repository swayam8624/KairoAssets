from pathlib import Path
repls = {
    'TextureColorSpace': 'TextureAuthoringColorSpace',
    'TextureSemantic': 'TextureAuthoringSemantic',
    'TextureMipPolicy': 'TextureAuthoringMipPolicy',
}
for name in ['MaterialAuthoring.cppm','EditableMeshDocument.cppm','tests/AuthoringDocumentTests.cpp']:
    p = Path(name)
    s = p.read_text()
    for old,new in repls.items(): s = s.replace(old,new)
    p.write_text(s)
Path('.github/workflows/rename-texture-authoring-enums.yml').unlink()
Path('.github/scripts/rename_texture_authoring_enums.py').unlink()
