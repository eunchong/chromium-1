# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import json

from telemetry.core import exceptions
from telemetry.value import scalar

from metrics import Metric


METRICS = {'privateMemory': {'units': 'MB', 'display_name': 'private_memory'},
           'cpu': {'units': '%', 'display_name': 'cpu_utilization'}}


class MediaRouterCPUMemoryMetric(Metric):
  "A metric for media router CPU/Memory usage."

  def Start(self, page, tab):
    raise NotImplementedError()

  def Stop(self, page, tab):
    raise NotImplementedError()

  def AddResults(self, tab, results):
    results_json = None
    try:
      results_json = tab.EvaluateJavaScript(
          'JSON.stringify(window.perfResults)')
    except exceptions.EvaluateException:
      pass
    # This log gives the detailed information about CPU/memory usage.
    logging.info('results_json' + ': ' + str(results_json))

    if not results_json:
      return
    perf_results = json.loads(results_json)
    for (metric, metric_results) in perf_results.iteritems():
      for (process, process_results) in metric_results.iteritems():
        if not process_results:
          continue
        # Get rid of 0 values
        non_zero_results = [result for result in process_results if result]
        if non_zero_results:
          avg_result = round(sum(non_zero_results)/len(non_zero_results), 4)
        else:
          avg_result = 0
        if metric == 'privateMemory':
          avg_result = round(avg_result/(1024 * 1024), 2)
        results.AddValue(scalar.ScalarValue(
            results.current_page,
            '%s_%s' % (METRICS.get(metric).get('display_name'), process),
            METRICS.get(metric).get('units'),
            avg_result))

    # Calculate MR extension wakeup time
    if 'mr_extension' in perf_results['cpu']:
      wakeup_percentage = round(
          (len(perf_results['cpu']['mr_extension']) * 100 /
           len(perf_results['cpu']['browser'])), 2)
      results.AddValue(scalar.ScalarValue(
              results.current_page, 'mr_extension_wakeup_percentage',
              '%', wakeup_percentage))