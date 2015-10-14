class sts_thread
{
public:
  sts_thread();

private:
  class sts_task
  {
    public:
    virtual void run() = 0;
  };

  template <typename Task>
  class sts_task_impl : public sts_task
  {
  public:
    sts_task_impl(Task t) :task(t) {}
    Task task;
    void run() {task(0);}
  };
public:
  sts_task *current_sts_task;
  template <typename Task>
  void set_task(Task t)
  {
    current_sts_task = new sts_task_impl<Task>(t);
  }
};
