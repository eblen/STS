#include <thread>
#include "sts_thread.h"

sts_thread::sts_thread() :cpp_thread(nullptr), current_sts_task(nullptr)
{
  cpp_thread = new std::thread([&] () {
  while(1)
  {
    if (current_sts_task != nullptr)
    {
      current_sts_task->run();
      current_sts_task = nullptr;
    }
  }});
}
