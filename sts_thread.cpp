#include <thread>
#include "sts_thread.h"
#include "sts.h"

sts_thread::sts_thread(sts *s) :scheduler(s), cpp_thread(nullptr), next_sts_task(null_task), task_start_iter(0),
                                                                                             task_end_iter(0)
{
  cpp_thread = new std::thread([&] () {
  sts_task *current_sts_task = null_task;
  while(1)
  {
    if (next_sts_task.load() != null_task)
    {
      current_sts_task = next_sts_task.exchange(null_task);
      if (!is_for_loop) current_sts_task->run();
      else
      {
        int i;
        for (i=task_start_iter; i<=task_end_iter; i++)
        {
          current_sts_task->run(i);
        }
      }
      current_sts_task = null_task;
      scheduler->record_task_part_done(task_name);
    }
  }});
}
