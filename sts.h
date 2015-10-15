#include <thread>
#include <vector>
#include <map>
#include "sts_thread.h"

typedef struct task_part_struct
{
  int start;
  int end;
  int id;
} task_part;

class sts
{
public:
  sts(int nt);
  std::thread::id get_id() const;
  void assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num);
  template <typename Task>
  void parallel_for(std::string task_name, Task task);

private:
  int num_threads;
  std::vector<sts_thread *> thread_pool;
  std::map< std::string, std::vector<task_part> > task_map;
};

template <typename Task>
void sts::parallel_for(std::string task_name, Task task)
{
  auto tplist_iter = task_map.find(task_name);
  if (tplist_iter == task_map.end())
  {
    std::cerr << "Error - Request to run unknown task" << std::endl;
    return;
  }

  int i;
  auto tplist = (*tplist_iter).second;
  for (i=0; i<tplist.size(); i++)
  {
    task_part tp = tplist[i];
    sts_thread *t = thread_pool[tp.id];
    t->set_for_task(task, tp.start, tp.end);
  }
}
