from pathlib import Path
p = Path('EditableMeshDocument.cppm')
s = p.read_text()
s = s.replace('#include <string_view>\n#include <vector>', '#include <string_view>\n#include <type_traits>\n#include <variant>\n#include <vector>')
s = s.replace('import Kairo.Assets.MaterialArtifact;\nimport Kairo.Assets.Types;', 'import Kairo.Assets.MaterialArtifact;\nimport Kairo.Assets.Metadata;\nimport Kairo.Assets.Types;')
p.write_text(s)
Path('.github/workflows/fix-editable-document-imports.yml').unlink()
Path('.github/scripts/fix_editable_document_imports.py').unlink()
