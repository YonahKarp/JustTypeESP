# Minimal English spellcheck data

Source: SCOWL 2020.12.07, https://downloads.sourceforge.net/wordlist/scowl-2020.12.07.tar.gz
Included source lists: English and American words through size 35, plus English
contractions through size 35. Proper names and acronyms are omitted. Original
notices are preserved in Copyright and copied beside the generated lexicon.

Run `python tools/spellcheck/build_dictionary.py` to rebuild offline from the
included lists. It minimizes a trie into an acyclic word automaton and packs each
arc into three bytes (17-bit target, 5-bit label, terminal and last-arc bits).
The hard budget is 100 KiB, including the root and word-count metadata.
Current output: 49,660 words, 101,038 bytes including the serialized root.
Only the generated header is compiled into firmware; source word lists and
Python tools are development files.

Tests: `python tools/spellcheck/test_dictionary.py` compares the full accepted
language against the input list. `tools/keyboard_tests/run.cmd` also exercises
the actual C++ recognizer against all words and tests normalization/tokenization.
