"""Build a minimal acyclic word automaton from SCOWL 2020.12.07 size 35.
Source: https://downloads.sourceforge.net/wordlist/scowl-2020.12.07.tar.gz
Includes common English and American forms; see Copyright for redistribution.
"""
from pathlib import Path
import re
import struct
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(__file__).resolve().parent

def generate():
    words=set()
    for file in sorted(list(SOURCE.glob('*-words.*'))+list(SOURCE.glob('*-contractions.*'))):
        for word in file.read_text(encoding='latin1').splitlines():
            # Exclude proper names/acronyms. Keep I and contractions, fold case at lookup.
            if word[:1].isupper() and word not in ('I',"I'm","I'll","I've","I'd"):
                continue
            word=word.lower()
            if re.fullmatch("[a-z]+(?:'[a-z]+)?",word):words.add(word)
    trie={}
    for word in sorted(words):
        node=trie
        for c in word:node=node.setdefault(c,{})
        node['']=True
    registry={};states=[]
    def minimize(node):
        edges=tuple((ord(c)-96 if c!="'" else 27,minimize(child)) for c,child in sorted(node.items()) if c)
        key=('' in node,edges)
        if key not in registry:registry[key]=len(states);states.append(key)
        return registry[key]
    root=minimize(trie)
    offsets={};arcs=[0]
    for i,(terminal,edges) in enumerate(states):
        offsets[i]=len(arcs) if edges else 0
        for j,(label,child) in enumerate(edges):
            arcs.append(offsets[child]|(label<<17)|(int(states[child][0])<<22)|(int(j==len(edges)-1)<<23))
    assert len(arcs)<(1<<17)
    data=struct.pack('<I',offsets[root])+b''.join(v.to_bytes(3,'little') for v in arcs)
    assert len(data)+4<=100*1024, 'Dictionary exceeds flash budget'
    out=ROOT/'main/justtype/src/spelling'
    out.mkdir(exist_ok=True)
    (out/'english.bin').write_bytes(data)
    (out/'english_lexicon.h').write_text('#pragma once\n#include <cstdint>\nnamespace english_lexicon {\nconstexpr uint32_t root = '+str(offsets[root])+';\nconstexpr uint32_t word_count = '+str(len(words))+';\nconstexpr uint8_t arcs[] = {\n'+''.join(','.join(hex(v) for v in data[4:][i:i+12])+',\n' for i in range(0,len(data)-4,12))+'};\n}\n')
    (out/'Copyright').write_bytes((SOURCE/'Copyright').read_bytes())
    (SOURCE/'expected_words.txt').write_text('\n'.join(sorted(words))+'\n')
    print(f'{len(words):,} words; {len(data):,} bytes; {len(states):,} minimized states')
    return words,data

if __name__=='__main__':generate()
