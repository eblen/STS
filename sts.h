#include <thread>
#include <vector>
#include "sts_thread.h"

class sts
{
public:
  sts(int nt);
  std::thread::id get_id() const;
  void assign(std::string task_name, int thread_num);
  void assign_for_iter(std::string task_name, int iter_num, int thread_num);
  template <typename Task>
  void parallel_for(std::string task_name, int start, int end, Task task);

private:
  int num_threads;
  std::vector<sts_thread *> thread_pool;
};

template <typename Task>
void sts::parallel_for(std::string task_name, int start, int end, Task task)
{
  int i;
  for (i=start; i<end; i++)
  {
    sts_thread *t = thread_pool[i];
    t->set_task(task);
  }
}
