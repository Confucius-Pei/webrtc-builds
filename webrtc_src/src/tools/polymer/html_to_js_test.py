#!/usr/bin/env python
# Copyright 2020 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import html_to_js
import os
import shutil
import tempfile
import unittest

_HERE_DIR = os.path.dirname(__file__)


class HtmlToJsTest(unittest.TestCase):
  def setUp(self):
    self._out_folder = None

  def tearDown(self):
    if self._out_folder:
      shutil.rmtree(self._out_folder)

  def _read_out_file(self, file_name):
    assert self._out_folder
    return open(os.path.join(self._out_folder, file_name), 'rb').read()

  def _run_test(self, js_file, js_out_file, js_out_file_expected):
    assert not self._out_folder
    self._out_folder = tempfile.mkdtemp(dir=_HERE_DIR)
    html_to_js.main([
        '--in_folder',
        os.path.join(_HERE_DIR, 'tests'), '--out_folder', self._out_folder,
        '--js_files', js_file
    ])

    actual_js = self._read_out_file(js_out_file)
    expected_js = open(
        os.path.join(_HERE_DIR, 'tests', js_out_file_expected), 'rb').read()
    self.assertEquals(expected_js, actual_js)

  def testHtmlToJs(self):
    self._run_test('v3_ready.js', 'v3_ready.js', 'v3_ready_expected.js')

  def testHtmlToTs(self):
    self._run_test('v3_ready.ts', 'v3_ready.ts', 'v3_ready_expected.ts')


if __name__ == '__main__':
  unittest.main()
