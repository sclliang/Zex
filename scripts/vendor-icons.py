"""Import pinned, offline file icons. Requires requests and fonttools; build needs neither."""
from pathlib import Path
from io import BytesIO
import json
import posixpath
import tarfile
import requests
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
MATERIAL_VERSION = "5.40.0"
VSCODE_COMMIT = "fcdfa8f173bf4371353cd3e154fa08d3ccf09596"
SETI_COMMIT = "2d6c5e68b4ded73c92dac291845ee44e1182d511"
OUTPUT = ROOT / "assets" / "file-icons"
NOTICES = ROOT / "vendor" / "icon-notices"
OUTPUT.mkdir(parents=True, exist_ok=True)
NOTICES.mkdir(parents=True, exist_ok=True)


def fetch(url):
    response = requests.get(url, timeout=90)
    response.raise_for_status()
    return response.content


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")


material_url = f"https://registry.npmjs.org/material-icon-theme/-/material-icon-theme-{MATERIAL_VERSION}.tgz"
with tarfile.open(fileobj=BytesIO(fetch(material_url)), mode="r:gz") as archive:
    source = json.load(archive.extractfile("package/dist/material-icons.json"))
    used = set(source["fileExtensions"].values()) | set(source["fileNames"].values()) | {source["file"]}
    material_light = source.get("light", {})
    used |= set(material_light.get("fileExtensions", {}).values()) | set(material_light.get("fileNames", {}).values())
    definitions = {}
    for key in sorted(used):
        relative = source["iconDefinitions"][key]["iconPath"]
        member = posixpath.normpath("package/dist/" + relative)
        if not member.startswith("package/icons/") or not member.endswith(".svg"):
            raise ValueError(member)
        destination = OUTPUT / "material" / Path(member).name
        destination.parent.mkdir(exist_ok=True)
        destination.write_bytes(archive.extractfile(member).read())
        definitions[key] = {"svg": "material/" + destination.name}
    write_json(OUTPUT / "material.json", {
        "default": source["file"], "definitions": definitions,
        "extensions": {k.lower(): v for k, v in source["fileExtensions"].items()},
        "names": {k.lower(): v for k, v in source["fileNames"].items()},
        "light": {"extensions": {k.lower(): v for k, v in material_light.get("fileExtensions", {}).items()},
                  "names": {k.lower(): v for k, v in material_light.get("fileNames", {}).items()}},
    })
    (NOTICES / "Material-Icon-Theme.txt").write_bytes(archive.extractfile("package/LICENSE").read())

base = f"https://raw.githubusercontent.com/microsoft/vscode/{VSCODE_COMMIT}"
seti = json.loads(fetch(base + "/extensions/theme-seti/icons/vs-seti-icon-theme.json"))
font = TTFont(BytesIO(fetch(base + "/extensions/theme-seti/icons/seti.woff")))
font.flavor = None
font.save(OUTPUT / "seti.ttf")
# VS Code supplies these common extensions through its language registrations,
# rather than its icon manifest. Resolve them explicitly for our standalone app.
language_extensions = {
    "c": "c", "cpp": "cpp cc cxx c++ hpp hxx hh h++ ino", "cuda-cpp": "cu cuh",
    "csharp": "cs csx", "python": "py pyw pyi ipynb", "javascript": "js mjs cjs",
    "javascriptreact": "jsx", "typescript": "ts mts cts", "typescriptreact": "tsx",
    "json": "json jsonl ndjson", "jsonc": "jsonc", "css": "css", "scss": "scss",
    "less": "less", "html": "html htm xhtml", "xml": "xml xsd xsl xslt svg qrc ui xaml plist",
    "markdown": "md markdown mdx", "yaml": "yaml yml", "properties": "ini cfg conf properties editorconfig",
    "powershell": "ps1 psm1 psd1", "bat": "bat cmd", "shellscript": "sh bash zsh fish ksh",
    "java": "java class jar", "rust": "rs", "go": "go", "ruby": "rb erb gemspec",
    "php": "php phtml", "perl": "pl pm", "lua": "lua", "sql": "sql sqlite db",
    "dart": "dart", "swift": "swift", "kotlin": "kt kts", "groovy": "groovy gvy",
    "clojure": "clj cljs cljc edn", "coffeescript": "coffee litcoffee", "fsharp": "fs fsx fsi",
    "r": "r rmd", "julia": "jl", "tex": "tex sty cls", "vue": "vue",
    "makefile": "mk mak cmake", "dockerfile": "dockerfile", "ignore": "gitignore gitattributes",
    "haskell": "hs lhs", "elixir": "ex exs", "elm": "elm", "bicep": "bicep",
    "terraform": "tf tfvars", "sass": "sass", "stylus": "styl", "objective-c": "m",
    "objective-cpp": "mm", "haxe": "hx", "ocaml": "ml mli", "rescript": "res resi",
}
extensions = {k.lower(): v for k, v in seti["fileExtensions"].items()}
for language, suffixes in language_extensions.items():
    for suffix in suffixes.split():
        extensions.setdefault(suffix, seti["languageIds"][language])
names = {k.lower(): v for k, v in seti["fileNames"].items()}
for name, language in {"dockerfile": "dockerfile", "containerfile": "dockerfile", "makefile": "makefile", "gnumakefile": "makefile", ".gitignore": "ignore", ".gitattributes": "ignore", ".bashrc": "shellscript", ".zshrc": "shellscript", ".env": "dotenv"}.items():
    names.setdefault(name, seti["languageIds"][language])
used = set(extensions.values()) | set(names.values()) | {seti["file"]}
# Resolve Seti's explicit light variants, including the language-derived aliases above.
light_source = seti["light"]
light_extensions = {k: (v + "_light" if v + "_light" in seti["iconDefinitions"] else v) for k, v in extensions.items()}
light_names = {k: (v + "_light" if v + "_light" in seti["iconDefinitions"] else v) for k, v in names.items()}
light_extensions.update({k.lower(): v for k, v in light_source.get("fileExtensions", {}).items()})
light_names.update({k.lower(): v for k, v in light_source.get("fileNames", {}).items()})
used |= set(light_extensions.values()) | set(light_names.values()) | {light_source["file"]}
definitions = {}
for key in sorted(used):
    definition = seti["iconDefinitions"][key]
    character = definition["fontCharacter"]
    if character.startswith("\\"):
        character = chr(int(character[1:], 16))
    definitions[key] = {"glyph": character, "color": definition.get("fontColor", "#a8b1c5")}
write_json(OUTPUT / "seti.json", {
    "default": seti["file"], "definitions": definitions,
    "extensions": extensions, "names": names,
    "light": {"default": light_source["file"], "extensions": light_extensions, "names": light_names},
})
(NOTICES / "VSCode-Seti.txt").write_bytes(fetch(base + "/LICENSE.txt"))
# The Seti font's original upstream MIT notice is also retained.
(NOTICES / "Seti-UI.txt").write_bytes(fetch(f"https://raw.githubusercontent.com/jesseweed/seti-ui/{SETI_COMMIT}/LICENSE.md"))
(NOTICES / "SOURCES.txt").write_text(
    f"Material Icon Theme {MATERIAL_VERSION}\n{material_url}\nhttps://github.com/material-extensions/vscode-material-icon-theme\n\n"
    f"Seti file associations and icon font from VS Code commit {VSCODE_COMMIT}\n{base}/extensions/theme-seti/icons/\n"
    f"Original font and glyphs: https://github.com/jesseweed/seti-ui (license commit {SETI_COMMIT})\n"
    "Zex converts WOFF to TTF without changing glyphs; Seti Mono changes only the display color.\n",
    encoding="utf-8")
entries = [f'    <file alias="{p.relative_to(OUTPUT).as_posix()}">file-icons/{p.relative_to(OUTPUT).as_posix()}</file>'
           for p in sorted(OUTPUT.rglob("*")) if p.is_file()]
(ROOT / "assets" / "file_icons.qrc").write_text(
    '<RCC>\n  <qresource prefix="/file-icons">\n' + "\n".join(entries) + '\n  </qresource>\n</RCC>\n', encoding="utf-8")
print(f"Embedded {len(entries)} resources; Material: {len(source['fileExtensions'])} extensions / {len(source['fileNames'])} filenames; Seti: {len(seti['fileExtensions'])} extensions / {len(seti['fileNames'])} filenames")
