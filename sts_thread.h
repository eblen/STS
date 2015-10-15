class sts_thread
{
public:
  sts_thread();

private:
  class sts_task
  {
    public:
    virtual void run(int iter) = 0;
  };

  template <typename Task>
  class sts_task_impl : public sts_task
  {
  public:
    sts_task_impl(Task t) :task(t) {}
    Task task;
    void run(int iter) {task(iter);}
  };
public:
  std::thread *cpp_thread;
  int task_start_iter;
  int task_end_iter;
  sts_task *current_sts_task;
  template <typename Task>
  void set_for_task(Task t, int start, int end)
  {
    task_start_iter = start;
    task_end_iter = end;
    current_sts_task = new sts_task_impl<Task>(t);
  }
  void wait(int thread_id);
};
