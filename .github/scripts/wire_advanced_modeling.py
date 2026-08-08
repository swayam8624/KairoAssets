from pathlib import Path

p = Path('AdvancedModelingOperations.cppm')
s = p.read_text().replace('#include <map>\n#include <queue>', '#include <map>\n#include <optional>\n#include <queue>')
p.write_text(s)

p = Path('CMakeLists.txt')
s = p.read_text()
s = s.replace('ModelingOperations.cppm\n    UVAuthoring.cppm', 'ModelingOperations.cppm\n    AdvancedModelingOperations.cppm\n    UVAuthoring.cppm')
s = s.replace('tests/AuthoringCompletionTests.cpp\n        tests/AuthoringDocumentTests.cpp', 'tests/AuthoringCompletionTests.cpp\n        tests/AdvancedModelingOperationsTests.cpp\n        tests/AuthoringDocumentTests.cpp')
p.write_text(s)

p = Path('KairoAssets.cppm')
s = p.read_text().replace('export import Kairo.Assets.ModelingOperations;\n', 'export import Kairo.Assets.ModelingOperations;\nexport import Kairo.Assets.AdvancedModelingOperations;\n')
p.write_text(s)

Path('.github/workflows/wire-advanced-modeling.yml').unlink()
Path('.github/scripts/wire_advanced_modeling.py').unlink()
