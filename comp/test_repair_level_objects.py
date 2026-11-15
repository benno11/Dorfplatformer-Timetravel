import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('repair', Path(__file__).with_name('repair-level-objects.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class RepairTest(unittest.TestCase):
    text = 'DFLVL4 3 2\n2 2 2 2 2 2\nOBJ 2\n59 16 16\n61 48 48\nOBJLAYER1 1\n1 3\n'
    reference = [[59,16,16],[61,48,48]]

    def test_corrupt_coordinates_repaired_without_changing_extensions(self):
        bad = self.text.replace('59 16 16','59 6868 16').replace('61 48 48','61 68 48')
        fixed, errors = m.repair_text(bad,self.reference,True)
        self.assertEqual(fixed,self.text)
        self.assertFalse(errors)
        self.assertTrue(m.repair_text(bad,self.reference)[1])

    def test_changed_object_sequence_is_not_guessed(self):
        bad = self.text.replace('59 16 16','60 6868 16')
        fixed, errors = m.repair_text(bad,self.reference,True)
        self.assertEqual(fixed,bad)
        self.assertTrue(errors)

    def test_valid_author_edits_and_fractional_parameters_are_preserved(self):
        edited = self.text.replace('59 16 16','59 80 16')
        self.assertEqual(m.repair_text(edited,self.reference,True),(edited,[]))
        fractional = self.text.replace('59 16 16','1100 12.5 15.75')
        self.assertEqual(m.repair_text(fractional,[],True),(fractional,[]))

    def test_nonfinite_and_incomplete_data(self):
        bad = self.text.replace('59 16 16','59 inf nan')
        self.assertEqual(m.repair_text(bad,self.reference,True),(self.text,[]))
        self.assertTrue(m.repair_text('DFLVL4 3 2\nOBJ 5\n',[],True)[1])

if __name__ == '__main__':
    unittest.main()
