"""Import the pinned Microsoft Codicons SVG subset used by Zex chrome."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib, json, urllib.request, xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
COMMIT = 'ceaa8663a7e63e2bac7f1ecac128ce31404b7487'
BASE = f'https://raw.githubusercontent.com/microsoft/vscode-codicons/{COMMIT}/'
ICONS = {
    'close': 'close', 'plus': 'add', 'minimize': 'chrome-minimize',
    'maximize': 'chrome-maximize', 'restore': 'chrome-restore', 'search': 'search',
    'up': 'chevron-up', 'down': 'chevron-down', 'right': 'chevron-right',
    'files': 'files', 'pin': 'pin', 'stop': 'debug-stop', 'split': 'split-horizontal',
    'list': 'list-flat', 'folder': 'folder-opened', 'wrap': 'word-wrap', 'lock': 'lock',
    'regex': 'regex', 'case': 'case-sensitive', 'whole': 'whole-word',
    'selection': 'list-selection', 'settings': 'settings-gear', 'file': 'file', 'follow': 'eye',
}
def fetch(path):
    with urllib.request.urlopen(BASE + path, timeout=30) as response:
        return response.read()
destination = ROOT / 'assets/ui/codicons'
notices = ROOT / 'vendor/icon-notices'
destination.mkdir(parents=True, exist_ok=True)
records = []
def download(pair):
    name, upstream = pair
    path = 'src/icons/' + upstream + '.svg'
    data = fetch(path)
    assert ET.fromstring(data).tag.endswith('svg')
    (destination / (name + '.svg')).write_bytes(data)
    return dict(name=name, upstream=path, sha256=hashlib.sha256(data).hexdigest())
with ThreadPoolExecutor(max_workers=6) as pool:
    records = list(pool.map(download, ICONS.items()))
(notices / 'Codicons-CC-BY-4.0.txt').write_bytes(fetch('LICENSE'))
(notices / 'Codicons-MIT-code.txt').write_bytes(fetch('LICENSE-CODE'))
(notices / 'Codicons-SOURCES.json').write_text(json.dumps(dict(
    project='Microsoft Codicons', author='Microsoft Corporation and contributors',
    source='https://github.com/microsoft/vscode-codicons', commit=COMMIT,
    adaptation='Original SVG paths retained; Zex applies its theme color at render time.',
    icons=records), indent=2) + '\n', encoding='utf-8')
qrc = ROOT / 'assets/ui_resources.qrc'
tree = ET.fromstring(qrc.read_bytes())
resource = next((x for x in tree if x.attrib.get('prefix') == '/chrome'), None)
if resource is not None:
    tree.remove(resource)
resource = ET.SubElement(tree, 'qresource', prefix='/chrome')
for name in ICONS:
    ET.SubElement(resource, 'file', alias=name+'.svg').text = 'ui/codicons/'+name+'.svg'
ET.indent(tree, space='  ')
qrc.write_text(ET.tostring(tree, encoding='unicode') + '\n', encoding='utf-8')
print(f'Imported {len(records)} icons from {COMMIT}')
