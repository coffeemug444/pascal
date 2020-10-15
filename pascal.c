#include "stdio.h"
#include "stdlib.h"
#include "longer-int.h"
#include <inttypes.h>
#include <time.h>
#include <stdbool.h> 
#include <string.h>
#include <sys/sysinfo.h>
#include <pthread.h>

pthread_mutex_t mutex_evens;
pthread_mutex_t mutex_odds;
pthread_mutex_t mutex_hashes;

typedef struct {
    LINT** row;
    LINT** previous_row;
    char* hash_table;
    LINT* evens;
    LINT* odds;

    int* positions;
    int num_workers;
} Context;

typedef struct {
    Context* context;
    int worker_num;
} Worker;

void free_row (LINT** row, int len) {
    for (int i = 0; i < len; i++) {
        free_lint(row[i]);
    }
    free(row);
}

// FNV-1a 64 bit hash on LINT
unsigned int hash_lint(LINT* num) {
    unsigned int prime = 16777619;
    unsigned int hash = 2166136261;

    for (unsigned int i = 0; i < num->used_size; i++) {
        char* ptr = (char*)&num->x[i];
        for (int c = 0; c < 4; c++) {
            hash ^= ptr[c];
            hash *= prime;
        }
    }

    return hash;
}

void print_most_common(char* hash_table) {
    LINT* largest = new_lint_str("0");
    LINT* tmp = new_lint_str("0");
    LINT* one = new_lint_str("1");
    int count = 0;
    init_lint_str(tmp, "1");

    // look through numbers 2 to 10,000 and check number of occurances
    // not too sure on the math maybe should check over 10,000?
    // this function is pretty quick so..
    for (int i = 2; i < 10000; i++) {
        add_lint(tmp, one);
        unsigned int hash = hash_lint(tmp);
        if (hash_table[hash] >= count) {
            count = hash_table[hash];
            copy_lint(largest, tmp);
        }
    }

    char* l = lint_itoa(largest);
    printf("%s had the most occurances at %d\n", l, count);
    free(l);
    free(largest);
    free(tmp);
}

void* worker_thread(void* arg) {
    Worker* worker = (Worker*)arg;
    LINT* tmp = new_lint_str("0");
    LINT* one = new_lint_str("1");
    LINT* two = new_lint_str("2");


    int start = worker->context->positions[worker->worker_num];
    int end = worker->context->positions[worker->worker_num + 1];

    //printf("starting at %d, ending at %d\n", start, end);

    // for each element in new row
    for (int j = start; j < end; j++) {
        // if this element is not the last in the row
        if (j != worker->context->positions[worker->context->num_workers] - 1) {
            worker->context->row[j] = clone_lint(worker->context->previous_row[j]);
        } else {
            // this element is the last in the row.
            // set it to 0, will be set to 1 in the next
            // instruction
            worker->context->row[j] = new_lint_str("0");
        }

        // if this element is not the first in the row
        if (j != 0) {
            add_lint(worker->context->row[j], worker->context->previous_row[j - 1]);
        }

        
        // if this element is even, increment evens etc.
        copy_lint(tmp, worker->context->row[j]);
        mod_lint(tmp, two);
        if (compare_lint(tmp, one) == 0) {
            pthread_mutex_lock(&mutex_odds);
            add_lint(worker->context->odds, one);
            pthread_mutex_unlock(&mutex_odds);
        } else {
            pthread_mutex_lock(&mutex_evens);
            add_lint(worker->context->evens, one);
            pthread_mutex_unlock(&mutex_evens);
        }

        // hash numbers that aren't 1
        if (compare_lint(worker->context->row[j],one)) {
            unsigned int hash = hash_lint(worker->context->row[j]);
            pthread_mutex_lock(&mutex_hashes);
            worker->context->hash_table[hash] += 1;
            pthread_mutex_unlock(&mutex_hashes);
        }
    }
}


int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "Usage: %s size print [y/n]\n", argv[0]);
        return 0;
    }
    int size = atoi(argv[1]);
    bool print = false;

    if (!strcmp(argv[2], "y")) {
        print = true;
    }



    // set up number counts
    // 32-bit hash table initialized to 0
    char* hash_table = malloc(4294967295 * sizeof(char));
    LINT* evens = new_lint_str("0");
    LINT* odds = new_lint_str("1");

    // set up rows
    LINT** row = malloc(size * sizeof(LINT*));;
    LINT** tmp_row;
    LINT** previous_row = malloc(size * sizeof(LINT*));
    previous_row[0] = new_lint_str("1");
    if (print) {
        printf("1,\n");
    }
    
    // thread stuff    
    int num_workers = get_nprocs() * 2;
    pthread_t workers[num_workers];
    Context context;
    pthread_mutex_init(&mutex_odds, NULL);
    pthread_mutex_init(&mutex_evens, NULL);
    pthread_mutex_init(&mutex_hashes, NULL);

    // timing stuff
    uint64_t nanoseconds;
	struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // for each new row
    for (int i = 2; i < size + 1; i++) {

        context.row = row;
        context.previous_row = previous_row;
        context.positions = malloc((num_workers + 1)*sizeof(int));
        context.positions[0] = 0;
        context.num_workers = num_workers;
        context.hash_table = hash_table;
        context.odds = odds;
        context.evens = evens;

        // find all the positions inside the row that the threads can start/end at
        int slice_size = i / num_workers;
        for (int p = 1; p < num_workers + 1; p++) {
            if (p != num_workers) {
                context.positions[p] = p * slice_size;
            } else {
                context.positions[p] = i;
            }
        }

        // put all the threads to work and then join them together
        Worker worker_contexts[num_workers];
        for (int p = 0; p < num_workers; p++) {
            worker_contexts[p].context = &context;
            worker_contexts[p].worker_num = p;
            pthread_create(&workers[p], NULL, worker_thread, &worker_contexts[p]);
        }
        for (int p = 0; p < num_workers; p++) {
            pthread_join(workers[p], NULL);
        }
        free(context.positions);


        // iterate through the row and print out all the values
        if (print) {
            for (int j = 0; j < i; j++) {
                char* l = lint_itoa(row[j]);
                printf("%s, ", l);
                free(l);
            }
        }

        if (print) {
            printf("\n");
        }

        // set up rows for next line
        tmp_row = previous_row;
        previous_row = row;
        row = tmp_row;
    }
    free_row(previous_row, size);
    free_row(row, size);

    print_most_common(hash_table);
    
    // more timing stuff
    clock_gettime(CLOCK_MONOTONIC, &end);
    nanoseconds = (end.tv_sec - start.tv_sec) * 1000000000ULL +
        (end.tv_nsec - start.tv_nsec);
    printf("Took %" PRIu64 " ms\n", nanoseconds / 1000000);

    char* even_s = lint_itoa(evens);
    char* odd_s = lint_itoa(odds);
    printf("%s evens, %s odds\n", even_s, odd_s);
    free(even_s);
    free(odd_s);

    free(hash_table);
    free(evens);
    free(odds);

    return 1;
}
