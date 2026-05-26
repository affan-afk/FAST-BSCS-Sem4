#include <stdio.h>
#include <stdlib.h>

int main(int argc,char *arg[]){
int numbers[] = {5,2,11,4,1};
printf("\nUNSORTED ARRAY: ");
    for (int i = 0; i < 5; i++)
    {
        //numbers[i] = atoi(arg[i]);
        printf(" %d",numbers[i]);
    }
    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 5-1; j++)
        {
            if (numbers[j] > numbers[j+1])
            {
                int temp = numbers[j];
                numbers[j] = numbers[j+1];
                numbers[j+1] = temp;
            }
        }
    }
    printf("\nSORTED ARRAY: ");
    for (int i = 0; i < 5; i++)
    {
        printf(" %d",numbers[i]);
    }
    return 0;
}