//used abbreviations CRIT = Critical, NORM=Normal, SER=Serious

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define SENIOR 2
#define JUNIOR 3
#define TOTAL (SENIOR+JUNIOR)
#define MAX_WAIT 30
#define PROMOTE 5

typedef enum { CRIT, SER, NORM } Type;
const char *T[]={"CRIT","SER","NORM"};

typedef struct P{
    int id; Type t; time_t arr;
    struct P *n;
}P;

typedef struct{ P *h,*t; int c; }Q;
typedef struct{ int id,type,cn; pthread_t tid; }D;

Q qc={0}, qs={0}, qn={0};
D doc[TOTAL];
int pid=1;

pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t c_any=PTHREAD_COND_INITIALIZER,
               c_crit=PTHREAD_COND_INITIALIZER;

void en(Q*q,P*p){ p->n=0; if(!q->t) q->h=q->t=p; else q->t=q->t->n=p; q->c++; }
P* de(Q*q){ if(!q->h) return 0; P*p=q->h; q->h=p->n; if(!q->h) q->t=0; q->c--; return p; }

void treat(D*d,P*p){
    printf("[D%d %s] P%d %s\n",d->id,d->type?"J":"S",p->id,T[p->t]);
    sleep(1); free(p);
}

void *patient(void *a){
    Type t=*(Type*)a; free(a);
    P*p=malloc(sizeof(P));

    pthread_mutex_lock(&m);
    p->id=pid++; p->t=t; p->arr=time(0);

    printf("[P%d] %s\n",p->id,T[t]);

    if(t==CRIT) en(&qc,p);
    else if(t==SER) en(&qs,p);
    else en(&qn,p);

    pthread_cond_signal(t==CRIT?&c_crit:&c_any);
    pthread_mutex_unlock(&m);
    return 0;
}

void *doctor(void *a){
    D*d=&doc[*(int*)a]; free(a);

    while(1){
        P*p=0;
        pthread_mutex_lock(&m);

        if(d->cn>=3 && qs.c){
            p=de(&qs); d->cn=0;
        }
        else if(!d->type && qc.c){
            p=de(&qc); d->cn=0;
        }
        else if(qs.c){
            p=de(&qs); d->cn=0;
        }
        else if(qn.c){
            p=de(&qn); d->cn++;
        }
        else{
            pthread_cond_wait(d->type?&c_any:&c_crit,&m);
            pthread_mutex_unlock(&m);
            continue;
        }

        pthread_mutex_unlock(&m);
        if(p) treat(d,p);
    }
}

void *promoter(void *a){
    while(1){
        sleep(1);
        pthread_mutex_lock(&m);
        if(qs.c>=PROMOTE){
            P*p=de(&qs); p->t=CRIT;
            en(&qc,p);
            printf("[PROMOTE] P%d\n",p->id);
            pthread_cond_signal(&c_crit);
        }
        pthread_mutex_unlock(&m);
    }
}

void check(Q*q){
    time_t now=time(0);
    for(P*p=q->h;p;p=p->n)
        if(difftime(now,p->arr)>MAX_WAIT){
            printf("[TIMEOUT] P%d\n",p->id);
            p->t=CRIT;
        }
}

void *timeout(void *a){
    while(1){
        sleep(5);
        pthread_mutex_lock(&m);
        check(&qs); check(&qn);
        pthread_mutex_unlock(&m);
    }
}

int main(){
    for(int i=0;i<TOTAL;i++){
        doc[i]=(D){i,i>=SENIOR,0};
        int *x=malloc(sizeof(int)); *x=i;
        pthread_create(&doc[i].tid,0,doctor,x);
    }

    pthread_t p1,p2;
    pthread_create(&p1,0,promoter,0);
    pthread_create(&p2,0,timeout,0);

    Type seq[]={CRIT,NORM,NORM,SER,SER,SER,SER,SER,NORM,CRIT}; 
    int n=sizeof(seq)/sizeof(Type);

    for(int i=0;i<n;i++){
        Type *x=malloc(sizeof(Type)); *x=seq[i];
        pthread_t t;
        pthread_create(&t,0,patient,x);
        pthread_detach(t);
        usleep(300000);
    }

    sleep(15);
    printf("\nDone\n");
    return 0;
}
