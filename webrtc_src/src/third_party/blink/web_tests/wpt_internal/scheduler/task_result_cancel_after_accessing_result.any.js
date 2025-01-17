// META: title=Scheduling API: Canceling Task Rejects Task.result (After Accessing Result)
// META: global=window,worker
'use strict';

async_test(t => {
  (function() {
    let tc = new TaskController();
    let task_promise = scheduler.postTask(() => 1234, { signal: tc.signal });
    task_promise.then(t.step_func((res) => {
      assert_true(false, 'task promise should not be fulfilled when the task is canceled.');
    }))
    .catch(t.step_func_done((e) => {
      assert_equals(e.name, 'AbortError');
    }));
    tc.abort();
  })();
}, 'Test task result is rejected properly when the task is canceled after accessing the result');
