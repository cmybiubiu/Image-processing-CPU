/* ------------
 * This code is provided solely for the personal and private use of
 * students taking the CSC367 course at the University of Toronto.
 * Copying for purposes other than this use is expressly prohibited.
 * All forms of distribution of this code, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Bogdan Simion, Maryam Dehnavi, Felipe de Azevedo Piovezan
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2020 Bogdan Simion and Maryam Dehnavi
 * -------------
*/

#include "filters.h"
#include <pthread.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

/************** FILTER CONSTANTS*****************/
/* laplacian */
int8_t lp3_m[] =
    {
        0, 1, 0,
        1, -4, 1,
        0, 1, 0,
    };
filter lp3_f = {3, lp3_m};

int8_t lp5_m[] =
    {
        -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1,
        -1, -1, 24, -1, -1,
        -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1,
    };
filter lp5_f = {5, lp5_m};

/* Laplacian of gaussian */
int8_t log_m[] =
    {
        0, 1, 1, 2, 2, 2, 1, 1, 0,
        1, 2, 4, 5, 5, 5, 4, 2, 1,
        1, 4, 5, 3, 0, 3, 5, 4, 1,
        2, 5, 3, -12, -24, -12, 3, 5, 2,
        2, 5, 0, -24, -40, -24, 0, 5, 2,
        2, 5, 3, -12, -24, -12, 3, 5, 2,
        1, 4, 5, 3, 0, 3, 5, 4, 1,
        1, 2, 4, 5, 5, 5, 4, 2, 1,
        0, 1, 1, 2, 2, 2, 1, 1, 0,
    };
filter log_f = {9, log_m};

/* Identity */
int8_t identity_m[] = {1};
filter identity_f = {1, identity_m};

filter *builtin_filters[NUM_FILTERS] = {&lp3_f, &lp5_f, &log_f, &identity_f};

typedef struct common_work_t
{
    const filter *f;
    const int32_t *original_image;
    int32_t *output_image;
    int32_t width;
    int32_t height;
    int32_t max_threads;
    pthread_barrier_t barrier;
} common_work;

typedef struct work_t
{
    common_work *common;
    int32_t id;
} work;

typedef struct queue_node_t {
    struct queue_node_t *next;
    int32_t row_start;
    int32_t row_end;
    int32_t col_start;
    int32_t col_end;
} queue_node;


pthread_mutex_t global_min_max_mutex;
pthread_mutex_t queue_mutex;

int32_t global_min = INT_MAX;
int32_t global_max = INT_MIN;

queue_node* q;
queue_node* q_normalization;
queue_node* q_cleanup;


/* Normalizes a pixel given the smallest and largest integer values
 * in the image */
void normalize_pixel(int32_t *target, int32_t pixel_idx, int32_t smallest,
        int32_t largest)
{
    if (smallest == largest)
    {
        return;
    }

    target[pixel_idx] = ((target[pixel_idx] - smallest) * 255) / (largest - smallest);
}
/*************** COMMON WORK ***********************/
/* Process a single pixel and returns the value of processed pixel
 * TODO: you don't have to implement/use this function, but this is a hint
 * on how to reuse your code.
 * */
int32_t apply2d(const filter *f, const int32_t *original, int32_t *target,
        int32_t width, int32_t height,
        int row, int column)
{
    // new pixel value
    int32_t pixel = 0;
    // coordinates of the upper left corner
    int32_t upper_left_row = row - f->dimension/2;
    int32_t upper_left_column = column - f->dimension/2;
    // multiplying the pixel values with the corresponding values in the Laplacian filter
    for (int r = 0; r < f->dimension; r ++) { // for each row
        for (int c = 0; c < f->dimension; c ++) { // for each col
            int32_t curr_row = upper_left_row + r;
            int32_t curr_col = upper_left_column + c;
            // Pixels on the edges and corners of the image do not have all 8 neighbors. Therefore only the valid
            // neighbors and the corresponding filter weights are factored into computing the new value.
            if (curr_row >= 0 && curr_col >= 0 && curr_row < height && curr_col < width) {
                int coord = curr_row * width + curr_col; // coordinate of the current pixel
                pixel += original[coord] * f->matrix[r * f->dimension + c];
            }
        }
    }
    return pixel;
}

/*********SEQUENTIAL IMPLEMENTATIONS ***************/
/* TODO: your sequential implementation goes here.
 * IMPORTANT: you must test this thoroughly with lots of corner cases and
 * check against your own manual calculations on paper, to make sure that your code
 * produces the correct image.
 * Correctness is CRUCIAL here, especially if you re-use this code for filtering
 * pieces of the image in your parallel implementations!
 */
void apply_filter2d(const filter *f,
        const int32_t *original, int32_t *target,
        int32_t width, int32_t height)
{
    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // loop through each pixel of the image and process it
    for(int r = 0; r < height; r ++) { // for each row
        for (int c = 0; c < width; c ++) { // for each col
            // process this pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) min = target[r * width + c];
            // look for max pixel value
            if (target[r * width + c] > max) max = target[r * width + c];
        }
    }

    // normalization
    for (int r = 0; r < height; r ++) {
        for (int c = 0; c < width; c ++) {
            normalize_pixel(target, r * width + c, min, max);
        }
    }
}

void update_global_min_max(int min, int max) {
    // update global min and global max for normalization
    pthread_mutex_lock(&global_min_max_mutex);
    if (min < global_min) global_min = min;
    if (max > global_max) global_max = max;
    pthread_mutex_unlock(&global_min_max_mutex);
}

void* horizontal_sharding(void *param) {
    work w = *(work*) param;

    // make a copy of the data on local stack
    int height = w.common->height;
    int width = w.common->width;
    int max_threads = w.common->max_threads;
    const filter *f = w.common->f;
    const int32_t *original = w.common->original_image;
    int32_t *target = w.common->output_image;

    // determine start row and end row
    int start_row = w.id * (height / max_threads); // inclusive
    int end_row = (w.id + 1) * (height / max_threads); // exclusive
    if (w.id == max_threads - 1) end_row = height;

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // horizontal sharding, row major
    for (int r = start_row; r < end_row; r ++) { // iterate through each row
        for (int c = 0; c < width; c ++) { // iterate through each column
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) min = target[r * width + c];
            // look for max pixel value
            if (target[r * width + c] > max) max = target[r * width + c];
        }
    }


    // update global min and global max for normalization
    update_global_min_max(min, max);

    // wait for all threads to be done with their work
    pthread_barrier_wait(&(w.common->barrier));

    // normalization
    for (int r = start_row; r < end_row; r ++) { // iterate through each row
        for (int c = 0; c < width; c ++) { // iterate through each column
            normalize_pixel(target, r * width + c, global_min, global_max);
        }
    }

    return NULL;

}

void* vertical_sharding_column_major(void *param) {
    work w = *(work*) param;

    // make a copy of the data on local stack
    int height = w.common->height;
    int width = w.common->width;
    int max_threads = w.common->max_threads;
    const filter *f = w.common->f;
    const int32_t *original = w.common->original_image;
    int32_t *target = w.common->output_image;

    // determine start column and end column
    int start_col = w.id * (width / max_threads); // inclusive
    int end_col = (w.id + 1) * (width / max_threads); // exclusive
    if (w.id == max_threads - 1) end_col = width;

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // vertical sharding column major
    for (int c = start_col; c < end_col; c ++) { // iterate through each column
        for (int r = 0; r < height; r ++) { // iterate through each row
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) min = target[r * width + c];
            // look for max pixel value
            if (target[r * width + c] > max) max = target[r * width + c];
        }
    }
    // update global min and global max for normalization
    update_global_min_max(min, max);

    // wait for all threads to be done with their work
    pthread_barrier_wait(&(w.common->barrier));

    // normalization
    for (int c = start_col; c < end_col; c ++) { // iterate through each column
        for (int r = 0; r < height; r ++) { // iterate through each row
            normalize_pixel(target, r * width + c, global_min, global_max);
        }
    }


    return NULL;
}

void* vertical_sharding_row_major(void *param) {
    work w = *(work*) param;

    // make a copy of the data on local stack
    int height = w.common->height;
    int width = w.common->width;
    int max_threads = w.common->max_threads;
    const filter *f = w.common->f;
    const int32_t *original = w.common->original_image;
    int32_t *target = w.common->output_image;


    // determine start column and end column
    int start_col = w.id * (width / max_threads); // inclusive
    int end_col = (w.id + 1) * (width / max_threads); // exclusive
    if (w.id == max_threads - 1) end_col = width;

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // vertical sharding row major
    for (int r = 0; r < height; r ++) { // iterate through each row
        for (int c = start_col; c < end_col; c ++) { // iterate through each col
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) min = target[r * width + c];
            // look for max pixel value
            if (target[r * width + c] > max) max = target[r * width + c];
        }
    }
    // update global min and global max for normalization
    update_global_min_max(min, max);

    // wait for all threads to be done with their work
    pthread_barrier_wait(&(w.common->barrier));

    // normalization
    for (int r = 0; r < height; r ++) { // iterate through each row
        for (int c = start_col; c < end_col; c ++) { // iterate through each row
            normalize_pixel(target, r * width + c, global_min, global_max);
        }
    }

    return NULL;
}


void* work_pool(void *param) {
    work w = *(work*) param;

    // make a copy of the data on local stack
    int height = w.common->height;
    int width = w.common->width;
    const filter *f = w.common->f;
    const int32_t *original = w.common->original_image;
    int32_t *target = w.common->output_image;

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // get a job from queue
    pthread_mutex_lock(&queue_mutex);
    while (q) {
        int32_t row_start = q->row_start;
        int32_t row_end = q->row_end;
        int32_t col_start = q->col_start;
        int32_t col_end = q->col_end;
        q = q->next;
        pthread_mutex_unlock(&queue_mutex);

        // process assigned image chunk
        for (int r = row_start; r < row_end; r ++) { // iterate through each row
            for (int c = col_start; c < col_end; c ++) { // iterate through each column
                // process each pixel
                target[r * width + c] = apply2d(f, original, target, width, height, r, c);
                // look for min pixel value
                if (target[r * width + c] < min) min = target[r * width + c];
                // look for max pixel value
                if (target[r * width + c] > max) max = target[r * width + c];
            }
        }
        pthread_mutex_lock(&queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);

    // update global min and global max for normalization
    update_global_min_max(min, max);

    // wait for all threads to be done with their work
    pthread_barrier_wait(&(w.common->barrier));

    // normalization
    pthread_mutex_lock(&queue_mutex);
    while (q_normalization) {
        int32_t row_start = q_normalization->row_start;
        int32_t row_end = q_normalization->row_end;
        int32_t col_start = q_normalization->col_start;
        int32_t col_end = q_normalization->col_end;
        q_normalization = q_normalization->next;
        pthread_mutex_unlock(&queue_mutex);

        for (int r = row_start; r < row_end; r ++) { // iterate through each row
            for (int c = col_start; c < col_end; c ++) { // iterate through each column
                normalize_pixel(target, r * width + c, global_min, global_max);
            }
        }
        pthread_mutex_lock(&queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);
    return NULL;
}

void create_work_queue(int32_t width, int32_t height, int32_t work_chunk) {
    q = (queue_node*)malloc(sizeof(queue_node));
    q->next = NULL;
    q_normalization = q;
    q_cleanup = q;
    queue_node* old_head = q;
    // initialize the chunk work queue
    for (int r = 0; r < ((height / work_chunk) + 1); ++r) {
        for (int c = 0; c < ((width / work_chunk) + 1); ++c) {
            q->row_start = work_chunk * r;
            q->row_end = work_chunk * (r + 1);
            if (q->row_end > height) q->row_end = height;

            q->col_start = work_chunk * c;
            q->col_end = work_chunk * (c + 1);
            if (q->col_end > width) q->col_end = width;

            // If loop won't run anymore no need to create another queue element
            if (r < (height / work_chunk) || c < (width / work_chunk)) {
                q->next = (queue_node*)malloc(sizeof(queue_node));
                q = q->next;
            }
        }
    }
    // Restore q pointer so that it still points to the head of the chunk
    // work queue
    q = old_head;
}

void clean_up_work_queue() {
    while(q_cleanup) {
        queue_node* head = q_cleanup;
        q_cleanup = q_cleanup->next;
        free(head);
    }
    // restore the global queue
    q = NULL;
    q_normalization = NULL;
    q_cleanup = NULL;
}


/***************** MULTITHREADED ENTRY POINT ******/
/* TODO: this is where you should implement the multithreaded version
 * of the code. Use this function to identify which method is being used
 * and then call some other function that implements it.
 */
void apply_filter2d_threaded(const filter *f,
        const int32_t *original, int32_t *target,
        int32_t width, int32_t height,
        int32_t num_threads, parallel_method method, int32_t work_chunk)
{
    // initialize common work
    common_work* cw = (common_work*)malloc(sizeof(common_work));
    cw->f = f;
    cw->original_image = original;
    cw->output_image = target;
    cw->width = width;
    cw->height = height;
    cw->max_threads = num_threads;
    pthread_barrier_init(&(cw->barrier) ,NULL, num_threads);

    // initialize work array
    work** threads_work = (work**)malloc(sizeof(work*) * num_threads);
    for (int i = 0; i < num_threads; i ++) {
        threads_work[i] = (work *) malloc(sizeof(work));
        threads_work[i]->common = cw;
        threads_work[i]->id = i;
    }

    pthread_t threads[num_threads];
    int rc;

    if (method == WORK_QUEUE) create_work_queue(width, height, work_chunk);

    for (int i = 0; i < num_threads; ++i) {
        if (method == SHARDED_ROWS) {
            rc = pthread_create(&threads[i], NULL, horizontal_sharding, (void *)threads_work[i]);
        }
        else if (method == SHARDED_COLUMNS_COLUMN_MAJOR) {
            rc = pthread_create(&threads[i], NULL, vertical_sharding_column_major, (void *)threads_work[i]);
        }
        else if (method == SHARDED_COLUMNS_ROW_MAJOR) {
            rc = pthread_create(&threads[i], NULL, vertical_sharding_row_major, (void *)threads_work[i]);
        }
        else if (method == WORK_QUEUE) {
            rc = pthread_create(&threads[i], NULL, work_pool, (void*)(threads_work[i]));
        }

        if (rc) exit(-1);
    }

    // All threads finish their job
    for (int i = 0; i < num_threads; ++i) {
        rc = pthread_join(threads[i], NULL);
        if (rc) exit(-1);
    }

    // clean up
    for (int i = 0; i < num_threads; ++i) {
        free(threads_work[i]);
    }
    free(threads_work);
    free(cw);
    pthread_barrier_destroy(&(cw->barrier));
    // restore global max and global min
    global_min = INT_MAX;
    global_max = INT_MIN;
    if (method == WORK_QUEUE) clean_up_work_queue();

}
