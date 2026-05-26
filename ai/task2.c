#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
int main(){
    pid_t leaf1,inner_node,leaf2,leaf3;
    printf("im root and my pid is %d\n",getpid());
    inner_node = fork();
        if (inner_node == 0)
        {
            leaf1 = fork()
        if (leaf1 == 0)
        {
            printf("im leaf 1 of inner node and my pid is %d\n",getpid());
            return 0;
        }
        leaf2 = fork();
        if (leaf2 == 0)
        {
            printf("im leaf 2 of inner node and my pid is %d\n",getpid());
            return 0;
        }
        wait(NULL);
        wait(NULL);
        return 0;
        }
        leaf3 = fork();
        if (leaf3 == 0)
        {
            printf("im leaf of root and my pid is %d\n",getpid());
            return 0;
        }
    wait(NULL);
    wait(NULL);
    return 0;
}