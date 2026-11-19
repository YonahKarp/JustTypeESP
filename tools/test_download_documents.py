import tempfile
import unittest
from pathlib import Path
import zlib
from download_documents import Receiver


class DownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.receiver = Receiver(self.root, lambda _: None)

    def tearDown(self):
        self.receiver.close()
        self.temp.cleanup()

    def header(self, name, size):
        self.receiver.accept(f"FILE {name.encode().hex()} {size}")

    def test_nested_binary_content_and_empty_document(self):
        content = "Café\nhello\r\n".encode()
        self.header("Folder/Test.txt", len(content))
        self.receiver.accept("DATA 0 " + content.hex())
        self.receiver.accept(f"END {zlib.crc32(content)}")
        self.header("Empty.txt", 0)
        self.receiver.accept("END 0")
        self.assertTrue(self.receiver.accept("DONE"))
        self.assertEqual((self.root / "Folder/Test.txt").read_bytes(), content)
        self.assertEqual((self.root / "Empty.txt").read_bytes(), b"")

    def test_corruption_does_not_publish_file(self):
        self.header("Test.txt", 1)
        self.receiver.accept("DATA 0 61")
        with self.assertRaises(ValueError):
            self.receiver.accept("END 0")
        self.receiver.close()
        self.assertFalse((self.root / "Test.txt").exists())
        self.assertFalse(list(self.root.glob("*.partial")))

    def test_rejects_missing_chunk(self):
        self.header("Test.txt", 4)
        with self.assertRaises(ValueError):
            self.receiver.accept("DATA 2 6162")

    def test_rejects_unsafe_paths(self):
        for name in ("../escape.txt", "/escape.txt", "C:/escape.txt", "a\\escape.txt"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.header(name, 0)

    def test_existing_documents_are_preserved(self):
        target = self.root / "Original.txt"
        target.write_text("original")
        with self.assertRaises(FileExistsError):
            self.header("Original.txt", 0)
        self.assertEqual(target.read_text(), "original")


if __name__ == "__main__":
    unittest.main()
