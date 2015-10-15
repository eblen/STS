#include "sts_thread.h"

sts_thread::sts_thread() :current_sts_task(nullptr)
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
