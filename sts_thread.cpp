#include <thread>
#include "sts_thread.h"
#include "sts.h"

int get_my_thread_id() {return 0;}

void set_affinity(int core_num)
{
  cpu_set_t cpuMask;
  CPU_ZERO(&cpuMask);
  CPU_SET(core_num, &cpuMask);
  sched_setaffinity(0, sizeof(cpu_set_t), &cpuMask);
}

sts_thread::sts_thread(sts *s, int id, int core_num) :scheduler(s), tid(id), cpp_thread(nullptr),
                                                      next_sts_task(nullptr), task_start_iter(0), task_end_iter(0)
{
  cpp_thread = new std::thread([&, core_num, this] () {
  sts_task *current_sts_task = nullptr;
  if (core_num != -1) set_affinity(core_num);
  while(1)
  {
    // Assumes that no other thread can change next task from non-null to null
    if (next_sts_task != nullptr)
    {
      current_sts_task = next_sts_task;
      {
        std::unique_lock<std::mutex> nst_lock(mutex_next_sts_task);
        next_sts_task = nullptr;
        cv_next_sts_task_open.notify_one();
      }
      scheduler->record_task_part_start(task_name);
      if (!is_for_loop) current_sts_task->run();
      else
      {
        int i;
        for (i=task_start_iter; i<=task_end_iter; i++)
        {
          current_sts_task->run(i);
        }
      }
      delete current_sts_task;
      current_sts_task = nullptr;
      scheduler->record_task_part_done(task_name);
    }
  }});
}
