# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import logging
import sys
from core import perf_benchmark
from core import platforms as core_platforms

import page_sets
from page_sets.system_health import platforms
from telemetry import benchmark
from telemetry import story as story_module
from telemetry.timeline import chrome_trace_category_filter
from telemetry.web_perf import timeline_based_measurement


RENDERING_BENCHMARK_UMA = [
    'Compositing.Display.DrawToSwapUs',
    'CompositorLatency.TotalLatency',
    'CompositorLatency.Type',
    'Event.Latency.ScrollBegin.Touch.TimeToScrollUpdateSwapBegin4',
    'Event.Latency.ScrollUpdate.Touch.TimeToScrollUpdateSwapBegin4',
    'Event.Latency.ScrollBegin.Wheel.TimeToScrollUpdateSwapBegin4',
    'Event.Latency.ScrollUpdate.Wheel.TimeToScrollUpdateSwapBegin4',
    'Graphics.Smoothness.Checkerboarding.TouchScroll',
    'Graphics.Smoothness.Checkerboarding.WheelScroll',
    'Graphics.Smoothness.Jank.AllAnimations',
    'Graphics.Smoothness.Jank.AllInteractions',
    'Graphics.Smoothness.Jank.AllSequences',
    'Graphics.Smoothness.PercentDroppedFrames.AllAnimations',
    'Graphics.Smoothness.PercentDroppedFrames.AllInteractions',
    'Graphics.Smoothness.PercentDroppedFrames.AllSequences',
    'Memory.GPU.PeakMemoryUsage2.Scroll',
    'Memory.GPU.PeakMemoryUsage2.PageLoad',
]


class _RenderingBenchmark(perf_benchmark.PerfBenchmark):
  # TODO(crbug/1205829): Capturing video is causing long cycle time and timeout
  # on some Pixel devices. Disabling this option until the issue can be fixed.
  #options = {
  #    'capture_screen_video': True
  #}

  @classmethod
  def AddBenchmarkCommandLineArgs(cls, parser):
    parser.add_option('--scroll-forever', action='store_true',
                      help='If set, continuously scroll up and down forever. '
                           'This is useful for analysing scrolling behaviour '
                           'with tools such as perf.')
    parser.add_option('--allow-software-compositing', action='store_true',
                      help='If set, allows the benchmark to run with software '
                           'compositing.')

  @classmethod
  def ProcessCommandLineArgs(cls, parser, args):
    cls.allow_software_compositing = args.allow_software_compositing

  def CreateStorySet(self, options):
    return page_sets.RenderingStorySet(platform=self.PLATFORM_NAME)

  def SetExtraBrowserOptions(self, options):
    options.AppendExtraBrowserArgs('--enable-gpu-benchmarking')
    options.AppendExtraBrowserArgs('--touch-events=enabled')
    if self.allow_software_compositing:
      logging.warning('Allowing software compositing. Some of the reported '
                      'metrics will have unreliable values.')
    else:
      options.AppendExtraBrowserArgs('--disable-software-compositing-fallback')

  def CreateCoreTimelineBasedMeasurementOptions(self):
    category_filter = chrome_trace_category_filter.CreateLowOverheadFilter()
    category_filter.AddDisabledByDefault(
        'disabled-by-default-histogram_samples')
    options = timeline_based_measurement.Options(category_filter)
    options.config.chrome_trace_config.EnableUMAHistograms(
        *RENDERING_BENCHMARK_UMA)
    options.SetTimelineBasedMetrics([
        'renderingMetric',
        'umaMetric',
        # Unless --experimentatil-tbmv3-metric flag is used, the following tbmv3
        # metrics do nothing.
        'tbmv3:uma_metrics'
    ])
    return options


@benchmark.Info(emails=['behdadb@chromium.org', 'jonross@chromium.org',
                        'sadrul@chromium.org'],
                documentation_url='https://bit.ly/rendering-benchmarks',
                component='Internals>GPU>Metrics')
class RenderingDesktop(_RenderingBenchmark):
  # TODO(rmhasan): Remove the SUPPORTED_PLATFORMS lists.
  # SUPPORTED_PLATFORMS is deprecated, please put system specifier tags
  # from expectations.config in SUPPORTED_PLATFORM_TAGS.
  SUPPORTED_PLATFORMS = [story_module.expectations.ALL_DESKTOP]
  SUPPORTED_PLATFORM_TAGS = [core_platforms.DESKTOP]
  PLATFORM_NAME = platforms.DESKTOP

  @classmethod
  def Name(cls):
    return 'rendering.desktop'

  def SetExtraBrowserOptions(self, options):
    super(RenderingDesktop, self).SetExtraBrowserOptions(options)
    # The feature below is only needed for macOS.
    # We found that the normal priorities used for mac is resulting into
    # unreliable values for avg_fps and frame_times. Increasing the priority
    # and using it in telemetry tests can help with more accurate values.
    # crbug.com/970607
    if sys.platform == 'darwin':
      options.AppendExtraBrowserArgs(
          '--use-gpu-high-thread-priority-for-perf-tests')


@benchmark.Info(emails=['behdadb@chromium.org', 'jonross@chromium.org',
                        'sadrul@chromium.org'],
                documentation_url='https://bit.ly/rendering-benchmarks',
                component='Internals>GPU>Metrics')
class RenderingMobile(_RenderingBenchmark):
  # TODO(rmhasan): Remove the SUPPORTED_PLATFORMS lists.
  # SUPPORTED_PLATFORMS is deprecated, please put system specifier tags
  # from expectations.config in SUPPORTED_PLATFORM_TAGS.
  SUPPORTED_PLATFORMS = [story_module.expectations.ALL_MOBILE]
  SUPPORTED_PLATFORM_TAGS = [core_platforms.MOBILE]
  PLATFORM_NAME = platforms.MOBILE

  @classmethod
  def Name(cls):
    return 'rendering.mobile'

  def SetExtraBrowserOptions(self, options):
    super(RenderingMobile, self).SetExtraBrowserOptions(options)
    # Disable locking the controls as visible for a minimum duration. This
    # allows controls to unlock after page load, rather than in the middle of a
    # story.
    options.AppendExtraBrowserArgs('--disable-minimum-show-duration')
    # Force online state for the offline indicator so it doesn't show and affect
    # the benchmarks on bots, which are offline by default.
    options.AppendExtraBrowserArgs(
        '--force-online-connection-state-for-indicator')

  def CreateCoreTimelineBasedMeasurementOptions(self):
    options = super(
        RenderingMobile, self).CreateCoreTimelineBasedMeasurementOptions()
    options.config.enable_platform_display_trace = True
    return options
