#include <thread>
#include <vector>
#include <cassert>
#include <map>
#include "sts_thread.h"

typedef struct task_part_struct
{
  bool is_for_loop;
  int start;
  int end;
  int id;
} task_part;

class sts
{
public:
  sts(int nt);
  std::thread::id get_id() const;
  void assign(std::string task_name, int thread_num);
  void assign_for_iter(std::string task_name, int iter_start, int iter_end, int thread_num);
  template <typename Task>
  void parallel(std::string task_name, Task task);
  template <typename Task>
  void parallel_for(std::string task_name, Task task);
  void wait(int thread_id);
  void wait_for_all();

private:
  int num_threads;
  std::vector<sts_thread *> thread_pool;
  std::map< std::string, std::vector<task_part> > task_map;
  std::vector<task_part> get_task_parts(std::string task_name);
};

template <typename Task>
void sts::parallel(std::string task_name, Task task)
{
  auto tplist = get_task_parts(task_name);
  assert(tplist.size() == 1);
  task_part tp = tplist[0];
  assert(!tp.is_for_loop);
  sts_thread *t = thread_pool[tp.id];
  t->set_task(task);
}

template <typename Task>
void sts::parallel_for(std::string task_name, Task task)
{
  auto tplist = get_task_parts(task_name);
  int i;
  for (i=0; i<tplist.size(); i++)
  {
    task_part tp = tplist[i];
    assert(tp.is_for_loop);
    sts_thread *t = thread_pool[tp.id];
    t->set_for_task(task, tp.start, tp.end);
  }
}
