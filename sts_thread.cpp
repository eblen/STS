#include "sts_thread.h"

sts_thread::sts_thread()
{
  while(1)
  {
    if (current_sts_task != nullptr)
    {
      current_sts_task->run();
      current_sts_task = nullptr;
    }
  }
}

