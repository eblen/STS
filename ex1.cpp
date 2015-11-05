#include <iostream>
#include <vector>
#include "sts.h"

int main(int argc, char **argv)
{
  // Sample data
  const int num_threads = 4;
  const int num_columns = 4;
  int matrix[num_threads][num_columns] = {{1,2,3,4},
                                          {1,2,3,4},
                                          {1,2,3,4},
                                          {1,2,3,4}};
  int result1[num_threads] = {0};
  int result2[num_threads];

  sts sched(num_threads);
  int i;
  std::vector<int> all_threads;
  for (i=0; i<num_threads; i++)
  {
    all_threads.push_back(i);
  }
  sched.assign_for_iter("for_loop_1", all_threads);
  sched.assign("add_task", 0);
  sched.assign("sub_task", 1);
  sched.assign("mul_task", 2);
  sched.assign("xor_task", 3);

  auto func = [&] (int i)
  {
    int j;
    for (j=0; j<num_columns; j++) result1[i] += matrix[i][j];
  };
  auto add_func = [&] ()
  {
    result2[0] = 0;
    int j;
    for (j=0; j<num_columns; j++) result2[0] += result1[j];
  };
  auto sub_func = [&] ()
  {
    result2[1] = 0;
    int j;
    for (j=0; j<num_columns; j++) result2[1] -= result1[j];
  };
  auto mul_func = [&] ()
  {
    result2[2] = 1;
    int j;
    for (j=0; j<num_columns; j++) result2[2] *= result1[j];
  };
  auto xor_func = [&] ()
  {
    result2[3] = 0;
    int j;
    for (j=0; j<num_columns; j++) result2[3] ^= result1[j];
  };

  for (i=0; i<2; i++)
  {
    sched.parallel_for("for_loop_1", num_columns, func);
    sched.wait("for_loop_1");
    sched.parallel("add_task", add_func);
    sched.parallel("sub_task", sub_func);
    sched.wait("add_task");
    sched.wait("sub_task");
    sched.parallel("mul_task", mul_func);
    sched.parallel("xor_task", xor_func);
    sched.wait("mul_task");
    sched.wait("xor_task");
  }

  printf("Result 1:");
  for (i=0; i<num_threads; i++) printf(" %d", result1[i]);
  printf("\nResult 2:");
  for (i=0; i<num_threads; i++) printf(" %d", result2[i]);
  printf("\n");
}
