#define _XOPEN_SOURCE 700
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define G 6.67430e-11

typedef struct { double m, x, y, vx, vy; } Body;
typedef struct { double x, y; } Vector;
typedef struct {
    int id, nthreads, n, steps, stride;
    double dt, tend;
    Body *bodies;
    Vector *forces;
    FILE *file;
    pthread_barrier_t *barrier;
} ThreadData;

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void write_state(ThreadData *d, double time) {
    if (d->file == NULL) return;
    fprintf(d->file, "%.10g", time);
    for (int i = 0; i < d->n; ++i)
        fprintf(d->file, ",%.17g,%.17g", d->bodies[i].x, d->bodies[i].y);
    fputc('\n', d->file);
}

static void *worker(void *argument) {
    ThreadData *d = argument;
    int first = d->id * d->n / d->nthreads;
    int last = (d->id + 1) * d->n / d->nthreads;
    Vector *my_forces = d->forces + d->id * d->n;

    for (int step = 0; step < d->steps; ++step) {
        memset(my_forces, 0, d->n * sizeof(Vector));

        /* Thread id owns rows first..last of the upper triangular matrix. */
        for (int i = first; i < last; ++i) {
            for (int j = i + 1; j < d->n; ++j) {
                double dx = d->bodies[j].x - d->bodies[i].x;
                double dy = d->bodies[j].y - d->bodies[i].y;
                double r = hypot(dx, dy);
                double k = G * d->bodies[i].m * d->bodies[j].m / (r * r * r);
                my_forces[i].x += k * dx;
                my_forces[i].y += k * dy;
                my_forces[j].x -= k * dx;
                my_forces[j].y -= k * dy;
            }
        }

        pthread_barrier_wait(d->barrier);

        double h = d->dt;
        if ((step + 1) * d->dt > d->tend) h = d->tend - step * d->dt;
        for (int i = first; i < last; ++i) {
            double fx = 0, fy = 0;
            for (int t = 0; t < d->nthreads; ++t) {
                fx += d->forces[t * d->n + i].x;
                fy += d->forces[t * d->n + i].y;
            }
            d->bodies[i].x += d->bodies[i].vx * h;
            d->bodies[i].y += d->bodies[i].vy * h;
            d->bodies[i].vx += fx / d->bodies[i].m * h;
            d->bodies[i].vy += fy / d->bodies[i].m * h;
        }

        pthread_barrier_wait(d->barrier);
        if (d->id == 0 && ((step + 1) % d->stride == 0 || step + 1 == d->steps))
            write_state(d, step + 1 == d->steps ? d->tend : (step + 1) * d->dt);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 7) {
        printf("Usage: %s tend input [threads=1 [dt=3600 [output=trajectory.csv [stride=1]]]]\n", argv[0]);
        return 1;
    }
    double tend = atof(argv[1]);
    int nthreads = argc > 3 ? atoi(argv[3]) : 1;
    double dt = argc > 4 ? atof(argv[4]) : 3600;
    const char *output_name = argc > 5 ? argv[5] : "trajectory.csv";
    int stride = argc > 6 ? atoi(argv[6]) : 1;
    if (tend < 0 || dt <= 0 || nthreads <= 0 || stride <= 0) return 1;

    FILE *input = fopen(argv[2], "r");
    if (input == NULL) { perror("input"); return 1; }
    int n;
    if (fscanf(input, "%d", &n) != 1 || n <= 0) return 1;
    Body *bodies = calloc(n, sizeof(Body));
    for (int i = 0; i < n; ++i)
        if (fscanf(input, "%lf %lf %lf %lf %lf", &bodies[i].m, &bodies[i].x,
                   &bodies[i].y, &bodies[i].vx, &bodies[i].vy) != 5) return 1;
    fclose(input);

    FILE *output = NULL;
    if (strcmp(output_name, "-") != 0) {
        output = fopen(output_name, "w");
        if (output == NULL) { perror("output"); return 1; }
        fprintf(output, "t");
        for (int i = 0; i < n; ++i) fprintf(output, ",x%d,y%d", i + 1, i + 1);
        fputc('\n', output);
    }

    pthread_t *threads = malloc(nthreads * sizeof(pthread_t));
    ThreadData *data = malloc(nthreads * sizeof(ThreadData));
    Vector *forces = calloc((size_t)nthreads * n, sizeof(Vector));
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, nthreads);
    int steps = (int)ceil(tend / dt);
    ThreadData common = {0, nthreads, n, steps, stride, dt, tend, bodies, forces, output, &barrier};
    if (output != NULL) write_state(&common, 0);

    double start = seconds();
    for (int t = 0; t < nthreads; ++t) {
        data[t] = common;
        data[t].id = t;
        pthread_create(&threads[t], NULL, worker, &data[t]);
    }
    for (int t = 0; t < nthreads; ++t) pthread_join(threads[t], NULL);
    double finish = seconds();

    if (output != NULL) fclose(output);
    pthread_barrier_destroy(&barrier);
    free(forces); free(data); free(threads); free(bodies);
    printf("Time: %.6f seconds\n", finish - start);
    return 0;
}
