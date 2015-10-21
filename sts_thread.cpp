#include <thread>
#include "sts_thread.h"
#include "sts.h"

void set_affinity(int core_num)
{
  cpu_set_t cpuMask;
  CPU_ZERO(&cpuMask);
  CPU_SET(core_num, &cpuMask);
  sched_setaffinity(0, sizeof(cpu_set_t), &cpuMask);
}

sts_thread::sts_thread(sts *s, int core_num) :scheduler(s), cpp_thread(nullptr), next_sts_task(null_task),
                                                                                 task_start_iter(0), task_end_iter(0)
{
  cpp_thread = new std::thread([&] () {
  sts_task *current_sts_task = null_task;
  set_affinity(core_num);
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
