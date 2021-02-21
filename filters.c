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

typedef struct queueNode {
    struct queueNode *next = NULL;
    //value:
    int32_t row_start;
    int32_t row_end;
    int32_t column_start;
    int32_t column_end;
} queueNode_t;

queueNode_t *queue;
queueNode_t *normalize_queue;
queueNode_t *dallocq;
int qleng;

pthread_mutex_t mutex;
int32_t global_min = INT_MAX;
int32_t global_max = INT_MIN;

int32_t largest = INT_MAX;
int32_t smallest = INT_MIN;

int enqueue(queueNode_t **head, int32_t row_start, int32_t row_end, int32_t column_start, int32_t column_end) {
    queueNode_t *new_node = malloc(sizeof(queueNode_t));
    if (!new_node) return -1;

    new_node->row_start = row_start;
    new_node->row_end = row_end;
    new_node->column_start = column_start;
    new_node->column_end = column_end;
    new_node->next = NULL;

    int queue_length = 1;

    //add new node to tail

    if (*head == NULL){ //head is null
        *head = new_node;
    } else {
        queueNode_t *current, *prev = NULL;
        current = *head;
        while (current->next != NULL) {
            prev = current;
            current = current->next;
            queue_length ++;
        }
        current->next = new_node;
        queue_length ++;
    }

    return queue_length;
}


queueNode_t* dequeue(queueNode_t **head) {
    if (*head == NULL) return NULL;

    queueNode_t *current = *head;

    *head = current->next;

    return current;
}

void destroy_queue(queueNode_t **head){
    if (*head == NULL) return;

    queueNode_t *current, *prev = NULL;

    current = *head;
    while (current->next != NULL) {
        prev = current;
        current = current->next;
        free(prev);
    }
    free(current);
}


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
            if (target[r * width + c] < min) {
                min = target[r * width + c];
            }
            // look for max pixel value
            if (target[r * width + c] > max) {
                max = target[r * width + c];
            }

        }
    }

    // normalization
    printf("sequencial: %d width: %d", height, width);
    for (int r = 0; r < height; r ++) {
        for (int c = 0; c < width; c ++) {
            printf("%d \n", target[r * width + c]);
            normalize_pixel(target, r * width + c, min, max);
        }
    }
}

/****************** ROW/COLUMN SHARDING ************/
/* TODO: you don't have to implement this. It is just a suggestion for the
 * organization of the code.
 */

/* Recall that, once the filter is applied, all threads need to wait for
 * each other to finish before computing the smallest/largets elements
 * in the resulting matrix. To accomplish that, we declare a barrier variable:
 *      pthread_barrier_t barrier;
 * And then initialize it specifying the number of threads that need to call
 * wait() on it:
 *      pthread_barrier_init(&barrier, NULL, num_threads);
 * Once a thread has finished applying the filter, it waits for the other
 * threads by calling:
 *      pthread_barrier_wait(&barrier);
 * This function only returns after *num_threads* threads have called it.
 */
// void* sharding_work(void *work)
// {
//     /* Your algorithm is essentially:
//      *  1- Apply the filter on the image
//      *  2- Wait for all threads to do the same
//      *  3- Calculate global smallest/largest elements on the resulting image
//      *  4- Scale back the pixels of the image. For the non work queue
//      *      implementations, each thread should scale the same pixels
//      *      that it worked on step 1.
//      */
//     return NULL;
// }


void update_global_min_max(int min, int max) {
    // update global min and global max for normalization
    pthread_mutex_lock(&mutex);
    if (min < global_min) {
        global_min = min;
    }
    if (max > global_max) {
        global_max = max;
    }
    pthread_mutex_unlock(&mutex);
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
    if (w.id == max_threads - 1) {
        end_row = height;
    }

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // horizontal sharding, row major
    printf("shart_row: %d end_row: %d\n", start_row, end_row);
    for (int r = start_row; r < end_row; r ++) { // iterate through each row
        for (int c = 0; c < width; c ++) { // iterate through each column
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) {
                min = target[r * width + c];
            }
            // look for max pixel value
            if (target[r * width + c] > max) {
                max = target[r * width + c];
            }
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
    if (w.id == max_threads - 1) {
        end_col = width;
    }

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // vertical sharding column major
    for (int c = start_col; c < end_col; c ++) { // iterate through each column
        for (int r = 0; r < height; r ++) { // iterate through each row
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) {
                min = target[r * width + c];
            }
            // look for max pixel value
            if (target[r * width + c] > max) {
                max = target[r * width + c];
            }
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
    if (w.id == max_threads - 1) {
        end_col = width;
    }

    // min and max pixel values for normalization
    int32_t min = INT_MAX;
    int32_t max = INT_MIN;

    // vertical sharding row major
    for (int r = 0; r < height; r ++) { // iterate through each row
        for (int c = start_col; c < end_col; c ++) { // iterate through each row
            // process each pixel
            target[r * width + c] = apply2d(f, original, target, width, height, r, c);
            // look for min pixel value
            if (target[r * width + c] < min) {
                min = target[r * width + c];
            }
            // look for max pixel value
            if (target[r * width + c] > max) {
                max = target[r * width + c];
            }
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


/***************** WORK QUEUE *******************/
/* TODO: you don't have to implement this. It is just a suggestion for the
 * organization of the code.
 */
 void* queue_work(void *param)
 {
     work w = *(work*) param;

     // make a copy of the data on local stack
     int height = w.common->height;
     int width = w.common->width;
     int max_threads = w.common->max_threads;
     const filter *f = w.common->f;
     const int32_t *original = w.common->original_image;
     int32_t *target = w.common->output_image;

     // min and max pixel values for normalization
     int32_t min = INT_MAX;
     int32_t max = INT_MIN;

     pthread_mutex_lock(&mutex);

     queueNode_t *current = dequeue(&queue);
     while (current != NULL ){
         int32_t row_start = current->row_start;
         int32_t row_end = current->row_end;
         int32_t column_start = current->column_start;
         int32_t column_end = current->column_end;
         current = dequeue(&queue);

         pthread_mutex_unlock(&mutex);

         if (row_end > height) row_end = height;
         if (column_end > width) column_end = width;
         if (row_start >= height) row_start = row_end;
         if (column_start >= column_end) column_start = column_end; //compare with width?
         if (row_start < 0) row_start = 0;
         if (column_start < 0) column_start = 0;

         for ( int row = row_start; row < row_end; row++){
             for ( int col = column_start; col < column_end; col++){
                 target[row * width + col] = apply2d(f, original, target, width, height, row, col);

                 if (target[row * width + col]  > max){
                     max = target[row * width + col] ;
                 }
                 if (target[row * width + col]  < min){
                     min = target[row * width + col] ;
                 }
             }
         }

         pthread_mutex_lock(&mutex);
     }

     pthread_mutex_unlock(&mutex);

     // wait for all threads to be done with their work
     // this barrier might not necessary
     pthread_barrier_wait(&(w.common->barrier));

     // update global min and global max for normalization
     update_global_min_max(min, max);

     // wait for all threads to be done with their work
     pthread_barrier_wait(&(w.common->barrier));

     // normalization
     pthread_mutex_lock(&mutex);
     queueNode_t *current_normal = dequeue(&normalize_queue);
     while (current_normal != NULL){
         int32_t row_start = current_normal->row_start;
         int32_t row_end = current_normal->row_end;
         int32_t column_start = current_normal->column_start;
         int32_t column_end = current_normal->column_end;
         current_normal = dequeue(&normalize_queue);

         pthread_mutex_unlock(&mutex);

         if (row_end > height) row_end = height;
         if (column_end > width) column_end = width;
         if (row_start >= height) row_start = row_end;
         if (column_start >= column_end) column_start = column_end; //compare with width?
         if (row_start < 0) row_start = 0;
         if (column_start < 0) column_start = 0;

         for ( int row = row_start; row < row_end; row++){
             for ( int col = column_start; col< column_end; col++){
                 normalize_pixel(target, row * width + col, smallest, largest);
             }
         }

         pthread_mutex_lock(&mutex);
     }

     pthread_mutex_unlock(&mutex);
 }


void create_queue(queueNode_t **head, int32_t width, int32_t height, int32_t work_chunk){

    int queue_length = 0;
    for (int i = 0;i < ((height/work_chunk)+1); i++){
        for ( int j = 0; j < ((width/work_chunk)+1);j++){

            int32_t row_start = work_chunk * i;
            int32_t row_end = work_chunk * (i+1);
            if (work_chunk > height){
                row_end = height;
            }

            int32_t column_start = work_chunk * j;
            int32_t column_end = work_chunk * (j+1);
            if(work_chunk > width){
                column_end = width;
            }

            queue_length = enqueue(&head, row_start, row_end, column_start, column_end);
        }
    }
    lazyfix = queue_length;
    qleng = queue_length;
    queue = head;
    normalize_queue = head;
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

    // initialize queue
    queueNode_t *head = (queueNode_t*)malloc(sizeof(queueNode_t));
    create_queue(&head, width, height, work_chunk);

    // initialize work array
    work** threads_work = (work**)malloc(sizeof(work*) * num_threads);
    for (int i = 0; i < num_threads; i ++) {
        threads_work[i] = (work *) malloc(sizeof(work));
        threads_work[i]->common = cw;
        threads_work[i]->id = i;
    }

    pthread_t threads[num_threads];
    int rc;

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
            // ???
            if (work_chunk >= width && work_chunk >= height){
                apply_filter2d_threaded(f, original, target, width, height, num_threads, SHARDED_ROWS, 1);
                return;
            }

            rc = pthread_create(&threads[i], NULL, queue_work, (void *)threads_work[i]);
            if (rc) exit(-1);

            rc = pthread_join(threads[i], NULL);
            if (rc) exit(-1);

        }

        if (rc) {
            fprintf(stderr, "return code from pthread_create() is %d\n", rc);
            exit(-1);
        }
    }

    // All threads finish their job
    if (method != WORK_QUEUE) {
        for (int i = 0; i < num_threads; ++i) {
            rc = pthread_join(threads[i], NULL);
            if (rc) exit(-1);
        }
    }



    printf("apply_filter2d_threaded height: %d width: %d \n", height, width);
    for (int r = 0; r < height; r ++) {
        for (int c = 0; c < width; c ++) {
            // printf("c: %d \n", c);
            printf("%d \n", cw->output_image[r * width + c]);
        }
    }


    // clean up
    for (int i = 0; i < num_threads; ++i) {
        free(threads_work[i]);
    }
    free(threads_work);
    free(cw);
    pthread_barrier_destroy(&(cw->barrier));
    destroy_queue(&head);
    // restore global max and global min
    global_min = INT_MAX;
    global_max = INT_MIN;

}
