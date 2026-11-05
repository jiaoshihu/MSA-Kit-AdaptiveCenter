import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('adapter', Path(__file__).with_name('adapter.py'))
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class AdapterTests(unittest.TestCase):
    def test_titles_iupac_and_tu_survive_reordering(self):
        source = [('same\t  描述', 'ARYTU'), ('same other', 'AGNTU')]
        output = [('msakit_00000001', 'AGN-TU'), ('msakit_00000000', 'ANNT-U')]
        self.assertEqual(adapter.restore_records(source, output),
                         [('same\t  描述', 'ARYT-U'), ('same other', 'AGN-TU')])

    def test_mutation_is_rejected_before_restoration(self):
        with self.assertRaisesRegex(ValueError, 'changed'):
            adapter.restore_records([('a', 'ARY')], [('msakit_00000000', 'NNN')])

    def test_missing_duplicate_and_extra_records(self):
        original = [('a', 'AN'), ('b', 'AT')]
        for result in [[], [('msakit_00000000', 'AN')], [('x', 'AN'), ('y', 'AT')],
                       [('msakit_00000000', 'AN'), ('msakit_00000000', 'AT')]]:
            with self.subTest(result=result), self.assertRaises(ValueError):
                adapter.restore_records(original, result)

    def test_bad_layout_is_rejected(self):
        for result in [[('msakit_00000000', 'A-N'), ('msakit_00000001', 'A-T')],
                       [('msakit_00000000', 'AN'), ('msakit_00000001', 'A-T')]]:
            with self.subTest(result=result), self.assertRaises(ValueError):
                adapter.restore_records([('a', 'AR'), ('b', 'AT')], result)

    def test_crlf_multiline_and_complete_titles(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'input.fa'
            path.write_bytes('>a\t two  words\r\nar\r\nytu\r\n>b\nAGNTU'.encode())
            self.assertEqual(adapter.read_records(path), [('a\t two  words', 'ARYTU'), ('b', 'AGNTU')])

    def test_invalid_input_and_duplicate_titles(self):
        for raw in ['>a\nA-C\n', '>a\nA.C\n', '>a\nAC X\n', '>a\nAC\n>a\nGT\n', 'AC\n', '>a\n']:
            with self.subTest(raw=raw), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / 'input.fa'
                path.write_text(raw)
                with self.assertRaises(ValueError):
                    adapter.read_records(path)


if __name__ == '__main__':
    unittest.main()
