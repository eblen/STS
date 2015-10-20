#include <thread>
#include "sts_thread.h"
#include "sts.h"

sts_thread::sts_thread(sts *s) :scheduler(s), cpp_thread(nullptr), current_sts_task(nullptr), task_start_iter(0),
                                                                                              task_end_iter(0)
{
  cpp_thread = new std::thread([&] () {
  while(1)
  {
    if (current_sts_task.load() != nullptr)
    {
      if (!is_for_loop) current_sts_task.load()->run();
      else
      {
        int i;
        for (i=task_start_iter; i<=task_end_iter; i++)
        {
          current_sts_task.load()->run(i);
        }
      }
      current_sts_task.store(nullptr);
      scheduler->record_task_part_done(task_name);
    }
  }});
}
