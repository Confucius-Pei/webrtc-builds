# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from page_sets.desktop_ui.js_utils import MEASURE_JS_MEMORY
from page_sets.desktop_ui.multitab_story import MultiTabStory
from page_sets.desktop_ui.ui_devtools_utils import ClickOn
from page_sets.desktop_ui.url_list import TOP_URL
from page_sets.desktop_ui.webui_utils import Inspect

WEBUI_TAB_STRIP_BENCHMARK_UMA = [
    'TabStrip.Tab.Views.ActivationAction',
    'TabStrip.Tab.WebUI.ActivationAction',
    'WebUITabStrip.CloseAction',
    'WebUITabStrip.CloseTabAction',
    'WebUITabStrip.LoadCompletedTime',
    'WebUITabStrip.LoadDocumentTime',
    'WebUITabStrip.OpenAction',
    'WebUITabStrip.OpenDuration',
    'WebUITabStrip.TabActivation',
    'WebUITabStrip.TabCreation',
    'WebUITabStrip.TabDataReceived',
]

WEBUI_TAB_STRIP_URL = 'chrome://tab-strip/'


class WebUITabStripStory(MultiTabStory):
  """Base class for webui tab strip stories"""

  def RunPageInteractions(self, action_runner):
    ClickOn(self._devtools, 'WebUITabCounterButton')
    action_runner = Inspect(action_runner.tab.browser, WEBUI_TAB_STRIP_URL)
    action_runner.ExecuteJavaScript(MEASURE_JS_MEMORY %
                                    'used_js_heap_size_begin')
    self.InteractWithPage(action_runner)
    action_runner.ExecuteJavaScript(MEASURE_JS_MEMORY % 'used_js_heap_size_end')

  def InteractWithPage(self, action_runner):
    self.ScrollTabs(action_runner)
    action_runner.Wait(5)

  def ScrollTabs(self, action_runner):
    action_runner.Wait(1)
    self.StartMeasuringFrameTime(action_runner, 'frame_time_on_scroll')
    action_runner.ScrollElement(element_function=SCROLL_ELEMENT_FUNCTION,
                                direction='left')
    self.StopMeasuringFrameTime(action_runner)
    action_runner.Wait(1)

  def WillStartTracing(self, chrome_trace_config):
    super(WebUITabStripStory, self).WillStartTracing(chrome_trace_config)
    chrome_trace_config.EnableUMAHistograms(*WEBUI_TAB_STRIP_BENCHMARK_UMA)


class WebUITabStripStoryCleanSlate(WebUITabStripStory):
  NAME = 'webui_tab_strip:clean_slate'
  URL_LIST = []
  URL = 'about:blank'
  WAIT_FOR_NETWORK_QUIESCENCE = False


class WebUITabStripStoryTop10(WebUITabStripStory):
  NAME = 'webui_tab_strip:top10:2020'
  URL_LIST = TOP_URL[:10]
  URL = URL_LIST[0]
  WAIT_FOR_NETWORK_QUIESCENCE = True


class WebUITabStripStoryTop10Loading(WebUITabStripStory):
  NAME = 'webui_tab_strip:top10:loading:2020'
  URL_LIST = TOP_URL[:10]
  URL = URL_LIST[0]
  WAIT_FOR_NETWORK_QUIESCENCE = False


class WebUITabStripStoryMeasureMemory(WebUITabStripStory):
  NAME = 'webui_tab_strip:measure_memory'
  URL_LIST = []
  URL = 'about:blank'
  WAIT_FOR_NETWORK_QUIESCENCE = False

  def WillStartTracing(self, chrome_trace_config):
    super(WebUITabStripStoryMeasureMemory,
          self).WillStartTracing(chrome_trace_config)
    chrome_trace_config.category_filter.AddExcludedCategory('*')
    chrome_trace_config.category_filter.AddIncludedCategory('blink.console')
    chrome_trace_config.category_filter.AddDisabledByDefault(
        'disabled-by-default-memory-infra')

  def GetExtraTracingMetrics(self):
    return super(WebUITabStripStoryMeasureMemory,
                 self).GetExtraTracingMetrics() + ['memoryMetric']

  def InteractWithPage(self, action_runner):
    action_runner.MeasureMemory(deterministic_mode=True)


SCROLL_ELEMENT_FUNCTION = '''
document.querySelector('tabstrip-tab-list')
'''
