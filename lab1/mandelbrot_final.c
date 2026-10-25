#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include "timer.h"


typedef struct {
    int thread_id;
    int start_i;
    int end_i;
    int grid_size;
    int npoints;
    int *found;
    FILE *file;
    pthread_mutex_t *mutex;
} ThreadData;

// Проверяем, принадлежит ли точка множеству Мандельброта
int is_mandelbrot(double a, double b) {
    double x = 0.0;
    double y = 0.0;
    int max_iter = 1000;

    for (int i = 0; i < max_iter; i++) {
        double new_x = x * x - y * y + a;
        double new_y = 2 * x * y + b;
        x = new_x;
        y = new_y;

        if (x * x + y * y > 4.0) {
            return 0;
        }
    }

    return 1;
}

// функция, которую выполняет каждый поток
void *worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;

    double xmin = -2.0;
    double xmax = 1.0;
    double ymin = -1.5;
    double ymax = 1.5;
    int grid_size = data->grid_size;

    for (int i = data->start_i; i < data->end_i; i++) {

        double a = xmin +
            (xmax - xmin) * i / (grid_size - 1);

        for (int j = 0; j < grid_size; j++){

            if (grid_size > 101 && i % 2 == 0 && j % 2 == 0) {
                continue;
            }

            double b = ymin +
                (ymax - ymin) * j / (grid_size - 1);

            if (is_mandelbrot(a, b)) {

                pthread_mutex_lock(data->mutex);

                if (*(data->found) < data->npoints) {
                    fprintf(data->file, "%.10f,%.10f\n", a, b);
                    (*(data->found))++;
                }

                pthread_mutex_unlock(data->mutex);
            }
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("Использование: ./mandelbrot nthreads npoints\n");
        return 1;
    }
    int nthreads = atoi(argv[1]);
    int npoints = atoi(argv[2]);

    if (nthreads <= 0 || npoints <= 0) {
        printf("Количество потоков и точек должно быть положительным\n");
        return 1;
    }

    FILE *file = fopen("points.csv", "w");

    if (file == NULL) {
        printf("Ошибка открытия файла\n");
        return 1;
    }
    fprintf(file, "x,y\n");

    int grid_size = 101;
    int found = 0;

    pthread_t *threads = malloc((size_t)nthreads * sizeof(pthread_t));
    ThreadData *data = malloc((size_t)nthreads * sizeof(ThreadData));

    if (threads == NULL || data == NULL) {
        printf("Ошибка выделения памяти\n");
        free(threads);
        free(data);
        fclose(file);
        return 1;
    }

    pthread_mutex_t mutex;
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        printf("Ошибка создания mutex\n");
        free(threads);
        free(data);
        fclose(file);
        return 1;
    }

    int error = 0;

    double start, finish;
    GET_TIME(start);

    while (found < npoints) {

        printf("Проверяем сетку %d x %d\n",
               grid_size, grid_size);

        int created = 0;


        for (int t = 0; t < nthreads; t++) {
            data[t].thread_id = t;

            data[t].start_i =
                (int)((long long)t * grid_size / nthreads);
            data[t].end_i =
                (int)((long long)(t + 1) * grid_size / nthreads);

            data[t].grid_size = grid_size;
            data[t].npoints = npoints;
            data[t].found = &found;
            data[t].file = file;
            data[t].mutex = &mutex;
            
            if (pthread_create(&threads[t], NULL, worker, &data[t]) != 0) {
                printf("Ошибка создания потока %d\n", t);
                error = 1;
                break;
            }
            created++;
        }
        for (int t = 0; t < created; t++) {
            pthread_join(threads[t], NULL);
        }

        if (error) {
            break;
        }

        if (found < npoints) {
            if (grid_size > (INT_MAX - 1) / 2) {
                printf("Размер сетки слишком большой\n");
                error = 1;
                break;
            }
            grid_size = 2 * (grid_size - 1) + 1;
        }
    }

    GET_TIME(finish);
    printf("Время выполнения: %.6f секунд\n", finish - start);
    
    fclose(file);
    printf("Найдено точек: %d из %d\n", found, npoints);
    printf("Результаты сохранены в points.csv\n");


    pthread_mutex_destroy(&mutex);
    free(threads);
    free(data);

    if (error || found != npoints) {
        return 1;
    }
    return 0;
}
