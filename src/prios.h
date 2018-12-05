
#ifndef _PRIOS_H
#define _PRIOS_H

int prios_init();
int prios_set_child_prio(int pid, int prio);
int prios_set_thread_prio(int prio);
extern size_t def_prio;

#endif
