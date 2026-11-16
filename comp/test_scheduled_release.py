import datetime as dt
import importlib.util
import json
import unittest
from unittest.mock import patch
from pathlib import Path
from zoneinfo import ZoneInfo
spec=importlib.util.spec_from_file_location('scheduled',Path(__file__).with_name('scheduled-prerelease.py'))
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
class ScheduleTest(unittest.TestCase):
 def test_local_time(self):
  due=dt.datetime.fromisoformat(json.loads((s.ROOT/'prerelease-plan.json').read_text())['publish_at'])
  self.assertEqual(due.astimezone(ZoneInfo('Europe/Berlin')).strftime('%Y-%m-%d %H:%M'),'2026-11-17 02:00')
 def test_before_or_after_no_mutation(self):
  for now in ['2026-11-17T00:59:59+00:00','2026-11-18T01:00:00+00:00']:
   with patch.object(s,'gh') as gh:
    s.run(now=dt.datetime.fromisoformat(now));gh.assert_not_called()
 def test_changed_commit_fails(self):
  with patch.object(s,'gh',return_value=json.dumps({'targetCommitish':'wrong'})):
   with self.assertRaises(ValueError):s.run(dry=True)
 def test_missing_package_fails(self):
  plan=json.loads((s.ROOT/'prerelease-plan.json').read_text())
  with patch.object(s,'gh',return_value=json.dumps({'targetCommitish':plan['commit'],'assets':[]})):
   with self.assertRaises(ValueError):s.run(dry=True)
