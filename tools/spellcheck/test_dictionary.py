import struct
import unittest
import build_dictionary

class DictionaryTests(unittest.TestCase):
    def test_exact_language_and_storage_budget(self):
        words,data=build_dictionary.generate()
        root=struct.unpack_from('<I',data)[0]
        arcs=[int.from_bytes(data[i:i+3],'little') for i in range(4,len(data),3)]
        def enumerate_words(node,prefix):
            if not node:return
            while True:
                arc=arcs[node]
                label=(arc>>17)&31
                word=prefix+("'" if label==27 else chr(96+label))
                if arc&(1<<22):yield word
                yield from enumerate_words(arc&0x1ffff,word)
                if arc&(1<<23):break
                node+=1
        self.assertEqual(set(enumerate_words(root,'')),words)
        self.assertLessEqual(len(data)+4,100*1024)
        for word in ['hello','world',"don't",'writing','keyboard','computer','running']:
            self.assertTrue(word in words,word)
        for word in ['helo','qzxqzx','spellchekk']:
            self.assertTrue(word not in words,word)

if __name__=='__main__':unittest.main()
