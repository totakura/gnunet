#ifndef __GAUGER_H__
#define __GAUGER_H__

#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

#define GAUGER(counter, value) {char __gauger_s[32];pid_t __gauger_p;if(!(__gauger_p=fork())){if(!fork()){sprintf(__gauger_s,"%d",value);execlp("gauger-cli.py","gauger-cli.py",counter, __gauger_s,(char*)NULL);perror("gauger");_exit(1);}else{_exit(0);}}else{waitpid(__gauger_p,NULL,0);}}

#endif
