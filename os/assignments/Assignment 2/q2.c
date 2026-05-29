#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h>
#include <float.h>
#include <string.h>

#define N 4 
#define TILE 2 
#define TILES (N / TILE)
#define M 2 
#define HOT 35.0
#define COLD -10.0
#define NAN_VAL -9999.0
#define THREADS 100

double sat[M][N][N], global[N][N], overlap[N][N], norm[N][N], risk[N][N];
double gmax, gmin, gmean, gvar;
int anomalies, hcount = 0, ccount = 0;

typedef struct { 
    int r;
    int c; 
} Cell;

Cell hot[N*N], cold[N*N];

typedef struct {
    double max;
    double min;
    double avg;
    double var;
    double std;
    int anom;
} Tile;
Tile tiles[TILES][TILES];
pthread_mutex_t m_merge = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_stat = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_hot = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_cold = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m_q = PTHREAD_MUTEX_INITIALIZER;

pthread_barrier_t b1, b2;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

int q[TILES*TILES][2], qh = 0, qt = 0, done = 0;

int isnanv(double v) {
    if (v == NAN_VAL) {
        return 1;
    } else {
        return 0;
    }
}
void *sat_thread(void *arg) {
    int s = *(int*)arg;
    free(arg);

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (isnanv(sat[s][r][c])) {
                double sum = 0; 
                int cnt = 0;
                int dr[4] = {-1, 1, 0, 0};
                int dc[4] = {0, 0, -1, 1};
                
                for (int d = 0; d < 4; d++) {
                    int nr = r + dr[d];
                    int nc = c + dc[d];
                    
                    if (nr >= 0 && nr < N && nc >= 0 && nc < N && !isnanv(sat[s][nr][nc])) {
                        sum += sat[s][nr][nc];
                        cnt++;
                    }
                }
                if (cnt != 0) {
                    sat[s][r][c] = sum / cnt;
                } else {
                    sat[s][r][c] = 0;
                }
            }
        }
    }   
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            pthread_mutex_lock(&m_merge);
            global[r][c] += sat[s][r][c];
            overlap[r][c] += 1;
            pthread_mutex_unlock(&m_merge);
        }
    }

    pthread_barrier_wait(&b1);
    return NULL;
}
void *tile_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&m_q);
        
        while (qh == qt && !done) {
            pthread_cond_wait(&cond, &m_q);
        }
        
        if (qh == qt && done) { 
            pthread_mutex_unlock(&m_q); 
            break; 
        }
        int tr = q[qh][0];
        int tc = q[qh][1];
        qh++;

        pthread_mutex_unlock(&m_q);
        int r0 = tr * TILE;
        int c0 = tc * TILE;
        double mx = -DBL_MAX;
        double mn = DBL_MAX;
        double sum = 0;

        for (int r = r0; r < r0 + TILE; r++) {
            for (int c = c0; c < c0 + TILE; c++) {
                double v = global[r][c];
                if (v > mx) {
                    mx = v;
                }
                if (v < mn) {
                    mn = v;
                }
                sum += v;
            }
        }
        double avg = sum / (TILE * TILE);
        double var = 0;
        double std;
        double an = 0;

        for (int r = r0; r < r0 + TILE; r++) {
            for (int c = c0; c < c0 + TILE; c++) {
                double d = global[r][c] - avg;
                var += d * d;
            }
        }
        var /= TILE * TILE;
        std = sqrt(var);

        for (int r = r0; r < r0 + TILE; r++) {
            for (int c = c0; c < c0 + TILE; c++) {
                if (fabs(global[r][c] - avg) > 2 * std) {
                    an++;
                }
            }
        }
        tiles[tr][tc] = (Tile){mx, mn, avg, var, std, an};

        pthread_mutex_lock(&m_stat);
        if (mx > gmax) {
            gmax = mx;
        }
        if (mn < gmin) {
            gmin = mn;
        }
        gmean += avg * TILE * TILE;
        gvar += var;
        anomalies += an;
        pthread_mutex_unlock(&m_stat);
    }
    return NULL;
}

void *hot_thread(void *arg) {
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (global[r][c] > HOT) {
                pthread_mutex_lock(&m_hot);
                hot[hcount].r = r;
                hot[hcount].c = c;
                hcount++;
                pthread_mutex_unlock(&m_hot);
            }
        }
    }
    pthread_barrier_wait(&b2);
    return NULL;
}

void *cold_thread(void *arg) {
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (global[r][c] < COLD) {
                pthread_mutex_lock(&m_cold);
                cold[ccount].r = r;
                cold[ccount].c = c;
                ccount++;
                pthread_mutex_unlock(&m_cold);
            }
        }
    }
    pthread_barrier_wait(&b2);
    return NULL;
}

void *norm_thread(void *arg) {
    double range = gmax - gmin; 
    if (!range) {
        range = 1;
    }
    
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            norm[r][c] = (global[r][c] - gmin) / range;
        }
    }
    pthread_barrier_wait(&b2);
    return NULL;
}

int dist(int r1, int c1, int r2, int c2) {
    return abs(r1 - r2) + abs(c1 - c2);
}

void *risk_thread(void *arg) {
    pthread_barrier_wait(&b2);

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            double ph = 0;
            double pc = 0;

            for (int i = 0; i < hcount; i++) {
                ph += 1.0 / (dist(r, c, hot[i].r, hot[i].c) + 1);
            }

            for (int i = 0; i < ccount; i++) {
                pc += 1.0 / (dist(r, c, cold[i].r, cold[i].c) + 1);
            }

            risk[r][c] = norm[r][c] * ph / (pc + 1);
        }
    }
    return NULL;
}

int main() {
    srand(42);

    for (int s = 0; s < M; s++) {
        for (int r = 0; r < N; r++) {
            for (int c = 0; c < N; c++) {
                if (rand() % 10 == 0) {
                    sat[s][r][c] = NAN_VAL;
                } else {
                    sat[s][r][c] = -20 + ((double)rand() / RAND_MAX) * 70;
                }
            }
        }
    }

    memset(global, 0, sizeof(global));
    memset(overlap, 0, sizeof(overlap));

    gmax = -DBL_MAX; 
    gmin = DBL_MAX;

    pthread_barrier_init(&b1, NULL, M + 1);

    pthread_t st[M];
    for (int i = 0; i < M; i++) {
        int *x = malloc(sizeof(int)); 
        *x = i;
        pthread_create(&st[i], NULL, sat_thread, x);
    }

    pthread_barrier_wait(&b1);
    
    for (int i = 0; i < M; i++) {
        pthread_join(st[i], NULL);
    }

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (overlap[r][c]) {
                global[r][c] /= overlap[r][c];
            }
        }
    }

    for (int i = 0; i < TILES; i++) {
        for (int j = 0; j < TILES; j++) {
            q[qt][0] = i;
            q[qt][1] = j;
            qt++;
        }
    }

    pthread_t rt[THREADS];
    for (int i = 0; i < THREADS; i++) {
        pthread_create(&rt[i], NULL, tile_thread, NULL);
    }

    pthread_mutex_lock(&m_q);
    done = 1;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&m_q);

    for (int i = 0; i < THREADS; i++) {
        pthread_join(rt[i], NULL);
    }

    gmean /= N * N;
    gvar /= TILES * TILES;

    pthread_barrier_init(&b2, NULL, 4);

    pthread_t A, B, C, R;
    pthread_create(&A, NULL, hot_thread, NULL);
    pthread_create(&B, NULL, cold_thread, NULL);
    pthread_create(&C, NULL, norm_thread, NULL);
    pthread_create(&R, NULL, risk_thread, NULL);

    pthread_join(A, NULL);
    pthread_join(B, NULL);
    pthread_join(C, NULL);
    pthread_join(R, NULL);

    printf("Max %.2f Min %.2f Mean %.2f Var %.4f\n", gmax, gmin, gmean, gvar);
    printf("Hot %d Cold %d Anom %d\n", hcount, ccount, anomalies);

    double top[10] = {0}; 
    int tr[10] = {0};
    int tc[10] = {0};

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            double v = risk[r][c];
            for (int k = 0; k < 10; k++) {
                if (v > top[k]) {
                    for (int j = 9; j > k; j--) {
                        top[j] = top[j - 1];
                        tr[j] = tr[j - 1];
                        tc[j] = tc[j - 1];
                    }
                    top[k] = v;
                    tr[k] = r;
                    tc[k] = c;
                    break;
                }
            }
        }
    }
    for (int i = 0; i < 10; i++) {
        printf("[%d,%d]=%.4f\n", tr[i], tc[i], top[i]);
    }
    return 0;
}