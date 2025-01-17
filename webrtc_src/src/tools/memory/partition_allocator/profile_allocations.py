#!/usr/bin/python3
# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Parses allocation profiles from a trace and graphs the results."""

import argparse
import gzip
import json
import logging
import os

from matplotlib import pylab as plt
import numpy as np


def _LoadTrace(filename: str) -> dict:
  """Loads a JSON trace, gzipped or not.

  Args:
    filename: Filename, gzipped or not.

  Returns:
    A dictionary with the trace content.
  """
  try:
    f = None
    if filename.endswith('.gz'):
      f = gzip.open(filename, 'r')
    else:
      f = open(filename, 'r')
    return json.load(f)
  finally:
    if f is not None:
      f.close()


def _ParseTrace(trace: dict) -> dict:
  """Parses a trace, and returns thread cache stats.

  Args:
    trace: As returned by _LoadTrace()

  Returns:
    {pid  -> {'name': str, 'labels': str, 'data': np.array}.
    Where the data array contains 'size' and 'count' columns.
  """
  events = trace['traceEvents']
  memory_infra_events = [
      e for e in events if e['cat'] == 'disabled-by-default-memory-infra'
  ]
  dumps = [
      e for e in memory_infra_events
      if e['name'] == 'periodic_interval' and e['args']['dumps']
      ['level_of_detail'] == 'detailed' and 'allocators' in e['args']['dumps']
  ]

  # Process names and labels.
  pid_to_name = {}
  pid_to_labels = {}

  metadata_events = [
      e for e in trace['traceEvents'] if e['cat'] == '__metadata'
  ]

  process_name_events = [
      e for e in metadata_events if e['name'] == 'process_name'
  ]
  for e in process_name_events:
    pid_to_name[e['pid']] = e['args']['name']

  process_labels_events = [
      e for e in metadata_events if e['name'] == 'process_labels'
  ]
  for e in process_labels_events:
    pid_to_labels[e['pid']] = e['args']['labels']

  result = {}
  for dump in dumps:
    pid = dump['pid']
    allocators = dump['args']['dumps']['allocators']

    # The browser process also has global dumps, we do not care about these.
    if 'global' in allocators:
      continue

    result[pid] = {
        'name': pid_to_name[pid],
        'labels': pid_to_labels.get(pid, '')
    }
    size_counts = []
    for allocator in allocators:
      if ('malloc/partitions/allocator/thread_cache/buckets_alloc/' not in
          allocator):
        continue
      size = int(allocator[allocator.rindex('/') + 1:])
      count = int(allocators[allocator]['attrs']['count']['value'], 16)
      size_counts.append((size, count))
      size_counts.sort()
      result[pid]['data'] = np.array(size_counts,
                                     dtype=[('size', np.int),
                                            ('count', np.int)])

  return result


def _PlotProcess(all_data: dict, pid: int, output_prefix: str):
  """Represents the allocation size distribution.

  Args:
    all_data: As returned by _ParseTrace().
    pid: PID to plot the data for.
    output_prefix: Prefix of the output file.
  """
  data = all_data[pid]
  logging.info('Plotting data for PID %d' % pid)

  # Allocations vs size.
  plt.figure(figsize=(16, 8))
  plt.title('Allocation count vs Size - %s - %s' %
            (data['name'], data['labels']))
  plt.stem(data['data']['size'], data['data']['count'])
  plt.xscale('log', base=2)
  plt.yscale('log', base=10)
  plt.xlabel('Size (log)')
  plt.ylabel('Allocations (log)')
  plt.savefig('%s_%d_count.png' % (output_prefix, pid), bbox_inches='tight')
  plt.close()

  # CDF.
  plt.figure(figsize=(16, 8))
  plt.title('CDF of allocation size - %s - %s' % (data['name'], data['labels']))
  cdf = np.cumsum(100. * data['data']['count']) / np.sum(data['data']['count'])

  for value in [512, 1024, 2048, 4096, 8192]:
    index = np.where(data['data']['size'] == value)[0]
    cdf_value = cdf[index]
    plt.axvline(x=value, ymin=0, ymax=cdf_value / 100., color='lightgrey')

  plt.step(data['data']['size'], cdf, color='black', where='post')
  plt.ylim(ymin=0, ymax=100)
  plt.xlim(xmin=10, xmax=1e6)
  plt.xscale('log', base=2)
  plt.xlabel('Size (log)')
  plt.ylabel('CDF (%)')
  plt.savefig('%s_%d_cdf.png' % (output_prefix, pid),
              bbox_inches='tight',
              dpi=300)
  plt.close()


def _CreateArgumentParser():
  parser = argparse.ArgumentParser()
  parser.add_argument(
      '--trace',
      type=str,
      required=True,
      help='Path to a trace.json[.gz] with memory-infra enabled.')
  parser.add_argument('--output-dir',
                      type=str,
                      required=True,
                      help='Output directory for graphs.')
  return parser


def main():
  logging.basicConfig(level=logging.INFO)
  parser = _CreateArgumentParser()
  args = parser.parse_args()

  logging.info('Loading the trace')
  trace = _LoadTrace(args.trace)

  logging.info('Parsing the trace')
  stats_per_process = _ParseTrace(trace)

  logging.info('Plotting the results')
  for pid in stats_per_process:
    if 'data' in stats_per_process[pid]:
      _PlotProcess(stats_per_process, pid,
                   os.path.join(args.output_dir, 'result'))


if __name__ == '__main__':
  main()
