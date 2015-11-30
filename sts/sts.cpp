#include "sts.h"

STS *STS::instance_ = nullptr;

//This is currently just a manual schedule for the example. Automatic scheduling isn't done yet.
void STS::reschedule()
{
    clearAssignments();
    bUseDefaultSchedule_ = false;

    assign("TASK_F", 1);
    assign("TASK_G", 2); 

    assign("TASK_F_0", 1, {0, {2,3}}); 
    
    assign("TASK_G_0", 2, {0, {1,2}});
    assign("TASK_G_1", 2, {0, {1,2}});
    
    assign("TASK_G_0", 0, {{1,2}, 1});
    assign("TASK_F_0", 0, {{2,3}, 1});
    assign("TASK_G_1", 0, {{1,2}, 1});

    nextStep();
}
