/*
 * STS example code 7
 * "Hello World" with nested coroutines
 */
#include "sts/sts.h"
#include "sts/thread.h"

STS *sts;

void task_f() {
    printf("H");
    sts->pause();
    printf("d!\n");
}

void task_g() {
    printf("e");
    sts->pause();
    printf("rl");
}

void task_h() {
    printf("ll");
    sts->pause();
    printf("o");
}

void task_i() {
    printf("o W");
}

int main(int argc, char **argv)
{
  STS::startup(1);
  sts = new STS();
  sts->clearAssignments();

  sts->assign_run("TASK_F", 0);
  sts->assign_run("TASK_G", 0);
  sts->assign_run("TASK_H", 0);
  sts->assign_run("TASK_I", 0);
  std::vector<int> t0 = {0};
  sts->setCoroutine("TASK_F", t0, "TASK_G");
  sts->setCoroutine("TASK_G", t0, "TASK_H");
  sts->setCoroutine("TASK_H", t0, "TASK_I");

  sts->nextStep();
  sts->run("TASK_F", task_f);
  sts->run("TASK_G", task_g);
  sts->run("TASK_H", task_h);
  sts->run("TASK_I", task_i);
  sts->wait();
  STS::shutdown();
}
