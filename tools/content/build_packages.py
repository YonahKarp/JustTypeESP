"""Build Leaf API fixtures and a fresh-device filesystem seed; never flashes."""
import argparse
import hashlib
import json
import re
import struct
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identifier(value):
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", value) or value in (".", ".."):
        raise ValueError(f"Invalid identifier: {value!r}")
    return value


def normalized(word):
    word = unicodedata.normalize("NFC", word.replace("’", "'").replace("‘", "'")).lower().strip("'")
    if not 1 <= len(word) <= 32 or len(word.encode()) > 96:
        return None
    if not all(c == "'" or 'a' <= c <= 'z' or '\u00df' <= c <= '\u00ff' and c != '÷' or 'а' <= c <= 'я' or c == 'ё' for c in word):
        return None
    return word


def wordlist(path):
    words = sorted({v for line in Path(path).read_text(encoding="utf-8").splitlines() if (v := normalized(line.strip()))}, key=lambda w: w.encode())
    if not words or len(words) > 100000:
        raise ValueError("Word list must contain 1..100000 supported words")
    data = b"LTXT\n" + b"".join(w.encode() + b"\n" for w in words)
    if len(data) > 1024 * 1024:
        raise ValueError("Dictionary exceeds 1 MiB; split/reduce vocabulary")
    return data


def english_dawg(path):
    words = sorted(set(Path(path).read_text(encoding='utf-8').splitlines()))
    if len(words) != 100000 or any(not re.fullmatch("[a-z]+(?:'[a-z]+)?", w) for w in words):
        raise ValueError('Expanded English requires 100,000 ASCII words')
    trie = {}
    for word in words:
        node = trie
        for char in word:
            node = node.setdefault(char, {})
        node[''] = True
    registry, states = {}, []
    def minimize(node):
        edges = tuple((27 if c == "'" else ord(c) - 96, minimize(child)) for c, child in sorted(node.items()) if c)
        key = ('' in node, edges)
        if key not in registry:
            registry[key] = len(states)
            states.append(key)
        return registry[key]
    root = minimize(trie)
    offsets, arcs = {}, [0]
    for i, (_, edges) in enumerate(states):
        offsets[i] = len(arcs) if edges else 0
        for j, (label, child) in enumerate(edges):
            arcs.append(offsets[child] | (label << 17) | (int(states[child][0]) << 22) | (int(j == len(edges) - 1) << 23))
    if len(arcs) >= 1 << 17:
        raise ValueError('Dictionary exceeds the DAWG address space')
    return b'LDWG' + struct.pack('<II', offsets[root], len(words)) + b''.join(v.to_bytes(3, 'little') for v in arcs)


def build(output, spec=None):
    output = Path(output)
    # The seed image includes only its manifest's assets. Old generated layouts
    # must not occupy fresh-device storage after a subsequent rebuild.
    seed_packages = output / 'seed' / 'leaf_content' / 'packages'
    if seed_packages.exists():
        import shutil
        for directory in seed_packages.iterdir():
            if directory.is_dir() and directory.name not in ('org.justtype.dictionary.en',):
                shutil.rmtree(directory)
    packages = []

    def add(meta, payloads):
        meta = dict(meta)
        for key in ('id', 'version'):
            identifier(meta[key])
        meta.setdefault('description', '')
        meta.setdefault('language', '')
        meta.setdefault('requires', [])
        if meta['type'] == 'theme' and 'theme.json' in payloads:
            meta['preview'] = payloads['theme.json'].decode('utf-8')
        meta['format'] = 1
        meta['files'] = []
        for name, data in payloads.items():
            identifier(name)
            cap = 1024 * 1024 if meta['type'] == 'dictionary' else 128 * 1024 if meta['type'] in ('font','glyphs') else 4096
            if not 0 < len(data) <= cap:
                raise ValueError(f"Asset size exceeds {cap}: {name}")
            if meta['type'] == 'font' and not re.fullmatch(r'(1[4-9]|[23][0-9]|40)\.bin', name):
                raise ValueError('Font filenames must be <pixel-size>.bin, 14..40')
            meta['files'].append(dict(name=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
            directories = [output / 'files' / meta['id'] / meta['version']]
            if meta['id'] in ('org.justtype.dictionary.en',):
                directories.append(output / 'seed' / 'leaf_content' / 'packages' / meta['id'] / meta['version'])
            for directory in directories:
                directory.mkdir(parents=True, exist_ok=True)
                (directory / name).write_bytes(data)
        packages.append(meta)

    if spec:
        spec = Path(spec).resolve()
        for item in json.loads(spec.read_text(encoding='utf-8'))['packages']:
            meta = {k: v for k, v in item.items() if k not in ('sources', 'wordlist')}
            payloads = {name: (spec.parent / source).read_bytes() for name, source in item.get('sources', {}).items()}
            if 'wordlist' in item:
                payloads['dictionary.bin'] = wordlist(spec.parent / item['wordlist'])
            add(meta, payloads)
    else:
        header = (ROOT / 'main/justtype/src/spelling/english_lexicon.h').read_text()
        root = int(re.search(r'root = (\d+)', header)[1])
        count = int(re.search(r'word_count = (\d+)', header)[1])
        arcs = bytes(int(h, 16) for h in re.findall(r'0x[0-9a-f]+', header))
        add(dict(id='org.justtype.dictionary.en', version='1.0.0', name='English (basic | 50k)', type='dictionary', language='en', description=f'{count:,} words; removable English spellcheck'), {'dictionary.bin': b'LDWG' + struct.pack('<II', root, count) + arcs})
        add(dict(id='org.justtype.dictionary.en.extended',version='1.1.0',name='English (expanded | 100k)',type='dictionary',language='en',description='100,000 words; compact English spellcheck'),{'dictionary.bin':english_dawg(ROOT/'tools/content/dictionaries/en.txt')})
        theme = dict(background='#18232d', edit_bg='#14202b', edit_text='#d9e7ef', cursor='#ffffff', sidebar_bg='#18232d', selection='#345a72')
        add(dict(id='org.justtype.theme.ink', version='1.0.0', name='Ink', type='theme', description='A quiet blue editor theme'), {'theme.json': json.dumps(theme).encode()})
        themes = {
            'parchment': ('Parchment', {'background':'#eee5d3','edit_bg':'#faf5e9','edit_text':'#403628','cursor':'#735c42','sidebar_bg':'#e9dfca','selection':'#dfc89d','sidebar_btn_text':'#403628'}),
            'midnight': ('Midnight', {'background':'#17192b','edit_bg':'#111322','edit_text':'#e0def1','cursor':'#c6b5ed','sidebar_bg':'#191b30','selection':'#48466b','sidebar_btn_text':'#e0def1'}),
            'rose': ('Rose', {'background':'#f2e3e3','edit_bg':'#fff6f4','edit_text':'#563d43','cursor':'#9c6874','sidebar_bg':'#efd9db','selection':'#e5bfc4','sidebar_btn_text':'#563d43'}),
            'ocean': ('Ocean', {'background':'#132839','edit_bg':'#10202e','sidebar_bg':'#132839','edit_text':'#d4eef0','sidebar_btn_text':'#d4eef0','text_dim':'#89b3bd','sidebar_btn_normal':'#1c3748','sidebar_btn_hover':'#305c70','sidebar_selected':'#31667a','sidebar_new_file':'#255366','selection':'#32647a','search_input_bg':'#1c3748','search_input_text':'#d4eef0'}),
            'forest': ('Forest', {'background':'#25382f','edit_bg':'#1a2c22','sidebar_bg':'#25382f','edit_text':'#e0ead4','sidebar_btn_text':'#e0ead4','text_dim':'#abc39f','sidebar_btn_normal':'#304838','sidebar_btn_hover':'#46664b','sidebar_selected':'#4a7254','sidebar_new_file':'#456447','selection':'#4c7054','search_input_bg':'#304838','search_input_text':'#e0ead4'}),
            'sunset': ('Sunset', {'background':'#f4dec9','text':'#282828','text_dim':'#8f6664','edit_bg':'#fff2df','edit_text':'#4a3038','cursor':'#c8c8c8','selection':'#e9bba6','sidebar_bg':'#efd1bc','sidebar_btn_normal':'#f6dfcd','sidebar_btn_hover':'#e4b69e','sidebar_btn_active':'#c3c3c3','sidebar_btn_text':'#4a3038','sidebar_btn_text_hover':'#141414','sidebar_selected':'#eab49e','sidebar_new_file':'#e4b09c','sidebar_delete':'#dc50a0','sidebar_cancel':'#c8c8c8','battery_border':'#646464','battery_good':'#50a050','battery_low':'#c85050','scrollbar_bg':'#e6e6e6','scrollbar_cursor':'#b4b4b4','search_input_bg':'#fff2df','search_input_text':'#4a3038','search_highlight':'#ffff96'}),
        }
        for slug,(name,values) in themes.items():
            add(dict(id='org.justtype.theme.'+slug,version='1.0.0',name=name,type='theme',description='Removable editor theme'),{'theme.json':json.dumps(values,separators=(',',':')).encode()})
        for slug,name in [('basic','Basic'),('ibmplexmono','IBM Plex Mono')]:
            add(dict(id='org.justtype.font.'+slug,version='1.0.0',name=name,type='font',description='Editor font in four sizes'),{f'{size}.bin':(ROOT/'tools/content/fonts'/slug/f'{size}.bin').read_bytes() for size in (16,20,24,28)})
        add(dict(id='org.justtype.glyphs.international.v2',version='1.0.0',name='International character support',type='glyphs',description='Shared accented Latin and Cyrillic glyphs'),{f'{size}.bin':(ROOT/'tools/content/international'/f'{size}.bin').read_bytes() for size in (14,16,18,20,24,28)})
        for slug,name in [('dvorak','Dvorak'),('dvorak-right','Dvorak right hand'),('colemak','Colemak'),('spanish','Spanish (Spain)'),('russian','Russian')]:
            add(dict(id='org.justtype.layout.'+slug,version='1.2.0' if slug in ('spanish','russian') else '1.0.0',name=name,type='layout',description='Removable keyboard layout',requires=['org.justtype.glyphs.international.v2@1.0.0'] if slug in ('spanish','russian') else []),{'layout.json':(ROOT/'tools/content/layouts'/f'{slug}.json').read_bytes()})
        add(dict(id='org.justtype.pack.english', version='1.0.0', name='English starter pack', type='pack', requires=['org.justtype.dictionary.en@1.0.0', 'org.justtype.theme.ink@1.0.0']), {})
    if len({p['id'] for p in packages}) != len(packages):
        raise ValueError('Duplicate package ID')
    catalog = json.dumps(dict(schema=1, packages=packages), ensure_ascii=False, separators=(',', ':')).encode()
    if len(catalog) > 64 * 1024:
        raise ValueError('Catalog exceeds 64 KiB')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'catalog.json').write_bytes(catalog)
    seed = output / 'seed' / 'leaf_content'
    seed.mkdir(parents=True, exist_ok=True)
    # Packs are available in the catalog; don't make default English depend on
    # an installed starter pack just to remove it.
    (seed / 'seed.json').write_text(json.dumps(dict(schema=1, packages=[p for p in packages if p['id'] in ('org.justtype.dictionary.en',)]), ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    (seed / 'catalog').mkdir(exist_ok=True)
    (seed / 'catalog/catalog.json').write_bytes(catalog)
    # Keep redistribution notices with the publishing output, outside the device payload.
    if not spec:
        (output / 'ENGLISH_LICENSE.txt').write_bytes((ROOT / 'main/justtype/src/spelling/Copyright').read_bytes())
        (output / 'INTERNATIONAL_FONT_LICENSE.txt').write_bytes((ROOT / 'tools/content/international/OFL.txt').read_bytes())
    return packages


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/content')
    parser.add_argument('--spec', type=Path, help='Optional custom package specification (paths relative to spec)')
    args = parser.parse_args()
    items = build(args.output, args.spec)
    print(f'Built {len(items)} packages in {args.output}; no device modified')
