"""Project-version synchronization checks."""
import unittest

from tools.sync_project_version import project_version, synchronize


class ProjectVersionTests(unittest.TestCase):
    def test_checked_in_references_match_canonical_version(self):
        self.assertRegex(project_version(), r"^[0-9]+\.[0-9]+\.[0-9]+$")
        self.assertEqual(synchronize(check=True), [])


if __name__ == "__main__":
    unittest.main()
