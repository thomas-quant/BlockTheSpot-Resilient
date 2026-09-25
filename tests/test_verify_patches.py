import configparser
import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from tools.verify_patches import apply_patch, parse_sig, verify


class VerifierTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.config = self.root / 'config.ini'
        (self.root / 'test.js').write_bytes(b'ABCD')
        self.base = '[Buffer_modify]\nEnable=1\n1=test.js\n[test.js]\n1=patch\n'
        self.patch = '[patch]\nSignature_1=41 42\nValue_1=FF\nOffset_1=1\n'

    def run_config(self, text):
        self.config.write_text(text, encoding='utf-8')
        with contextlib.redirect_stdout(io.StringIO()):
            return verify(self.config, self.root)

    def test_valid_and_inputs_unchanged(self):
        self.assertEqual(self.run_config(self.base + self.patch), 0)
        self.assertEqual((self.root / 'test.js').read_bytes(), b'ABCD')

    def test_missing_config(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(verify(self.config, self.root), 1)

    def test_missing_target_section(self):
        self.assertEqual(self.run_config('[Buffer_modify]\nEnable=1\n1=test.js\n'), 1)

    def test_enabled_empty_and_gapped_lists(self):
        self.assertEqual(self.run_config('[Buffer_modify]\nEnable=1\n'), 1)
        self.assertEqual(self.run_config(self.base.replace('1=test.js', '2=test.js') + self.patch), 1)

    def test_missing_file(self):
        (self.root / 'test.js').unlink()
        self.assertEqual(self.run_config(self.base + self.patch), 1)

    def test_duplicate_section(self):
        self.assertEqual(self.run_config(self.base + self.patch + self.patch), 1)

    def test_out_of_bounds_and_negative(self):
        for offset in ('4', '-1', '4294967295'):
            with self.subTest(offset=offset):
                self.assertEqual(self.run_config(self.base + self.patch.replace('Offset_1=1', f'Offset_1={offset}')), 1)

    def test_ambiguous_including_overlap(self):
        (self.root / 'test.js').write_bytes(b'AAA')
        self.assertEqual(self.run_config(self.base + self.patch.replace('41 42', '41 41')), 1)

    def test_second_signature_required_to_match(self):
        second = 'Signature_2=43 45\nValue_2=00\nOffset_2=0\n'
        self.assertEqual(self.run_config(self.base + self.patch + second), 1)
        self.assertEqual((self.root / 'test.js').read_bytes(), b'ABCD')

    def test_valid_pair(self):
        second = 'Signature_2=43 44\nValue_2=00\nOffset_2=0\n'
        self.assertEqual(self.run_config(self.base + self.patch + second), 0)

    def test_overlapping_pair(self):
        second = 'Signature_2=42 43\nValue_2=00\nOffset_2=0\n'
        self.assertEqual(self.run_config(self.base + self.patch + second), 1)

    def test_wildcards_ff_and_bad_tokens(self):
        self.assertEqual(parse_sig('FF ?? 00'), [255, None, 0])
        for text in ('', '100', '-1', 'F', 'GG', '??00'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_sig(text)

    def test_sequential_groups_use_patched_bytes(self):
        config = configparser.ConfigParser(interpolation=None)
        config.optionxform = str
        config.read_string(self.patch + '[second]\nSignature_1=41 FF\nValue_1=30\n')
        data = apply_patch(config, 'patch', b'ABCD')
        self.assertEqual(apply_patch(config, 'second', data), b'0\xffCD')

    def test_disabled_is_explicit(self):
        self.config.write_text('[Buffer_modify]\nEnable=0\n')
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            self.assertEqual(verify(self.config, self.root), 0)
        self.assertIn('NOT verified', output.getvalue())


if __name__ == '__main__':
    unittest.main()
