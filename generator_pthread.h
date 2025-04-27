#ifndef GENERATOR_PTHREAD_H
#define GENERATOR_PTHREAD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h> // For int64_t
#include <stdio.h>
#include <stdlib.h>
#include <errno.h> // For error checking

// --- Opaque Generator Type ---
typedef struct generator generator_t;

// --- Generator Function Type ---
// User function receives the generator object pointer
typedef void (*generator_func_t)(generator_t* self);




// --- Internal Details (Implementation Hiding) ---
// Keep the struct definition internal to the .c file or declare it here
// but emphasize it's for internal use. For simplicity, we define it here.

typedef enum {
    GEN_RUNNING,    // Generator thread is actively computing
    GEN_SUSPENDED,  // Generator thread is waiting in yield()
    GEN_FINISHED    // Generator thread function has returned
} generator_state_t;

struct generator {
    pthread_t thread_id;        // Generator thread identifier
    pthread_mutex_t mtx;        // Mutex for protecting shared data
    pthread_cond_t cond_yield;  // Signaled by caller (next) to wake generator (yield)
    pthread_cond_t cond_next;   // Signaled by generator (yield/finish) to wake caller (next)

    generator_func_t user_func; // User's generator function
    void* user_data;            // Data passed during creation
    int64_t yielded_value;      // Value passed via yield()
    generator_state_t state;    // Current state of the generator
    bool value_ready;           // Flag: Is yielded_value fresh?
    bool started;               // Flag: Has the generator thread started execution?
};


static void* generator_thread_entry(void* arg) {
    generator_t* self = (generator_t*)arg;

    // --- Initial wait for the first 'next' call ---
    pthread_mutex_lock(&self->mtx);
    // Mark as started *before* waiting, so destroy knows if join is needed
    self->started = true;
    // Wait until the first generator_next signals us to start
    while (self->state == GEN_SUSPENDED && self->started) { // Check started to handle immediate destroy
         // printf("[Thread %p] Waiting for initial signal...\n", (void*)pthread_self());
         pthread_cond_wait(&self->cond_yield, &self->mtx);
         // printf("[Thread %p] Initial signal received.\n", (void*)pthread_self());
         if (self->state == GEN_FINISHED) { // Check if destroyed before starting
             pthread_mutex_unlock(&self->mtx);
             return NULL;
         }
    }
     // Set state to running *after* being signaled to start
    if (self->state != GEN_FINISHED) { // Avoid race if destroyed quickly
        self->state = GEN_RUNNING;
    }
    pthread_mutex_unlock(&self->mtx);
    // --- End Initial wait ---


    // --- Execute User Function ---
    // Only run if not already marked as finished (e.g., by destroy)
    if (self->state == GEN_RUNNING) {
        // printf("[Thread %p] Calling user function...\n", (void*)pthread_self());
        self->user_func(self);
        // printf("[Thread %p] User function returned.\n", (void*)pthread_self());
    }
    // --- End User Function Execution ---


    // --- Finalization ---
    pthread_mutex_lock(&self->mtx);
    self->state = GEN_FINISHED;
    self->value_ready = true; // Signal completion
    // printf("[Thread %p] Signaling FINISHED to caller.\n", (void*)pthread_self());
    pthread_cond_signal(&self->cond_next); // Wake up the caller waiting in next()
    pthread_mutex_unlock(&self->mtx);
    // --- End Finalization ---

    return NULL;
}

// --- Public API ---

/**
 * @brief Creates a new generator running the user function in a separate thread.
 *
 * @param func User-provided generator function.
 * @param user_data Data to be passed to the generator via self->user_data.
 * @return Pointer to the new generator object, or NULL on failure.
 */
static generator_t* generator_create(generator_func_t func, void* user_data) {
    if (!func) {
        fprintf(stderr, "Error: Generator function cannot be NULL.\n");
        return NULL;
    }

    generator_t* gen = (generator_t*)malloc(sizeof(generator_t));
    if (!gen) {
        perror("malloc for generator_t failed");
        return NULL;
    }

    gen->user_func = func;
    gen->user_data = user_data;
    gen->state = GEN_SUSPENDED; // Start suspended, waiting for first next()
    gen->yielded_value = 0;
    gen->value_ready = false;
    gen->started = false;       // Thread not yet started execution logic

    // Initialize mutex and condition variables
    if (pthread_mutex_init(&gen->mtx, NULL) != 0) {
        perror("pthread_mutex_init failed");
        free(gen);
        return NULL;
    }
    if (pthread_cond_init(&gen->cond_yield, NULL) != 0) {
        perror("pthread_cond_init (yield) failed");
        pthread_mutex_destroy(&gen->mtx);
        free(gen);
        return NULL;
    }
    if (pthread_cond_init(&gen->cond_next, NULL) != 0) {
        perror("pthread_cond_init (next) failed");
        pthread_cond_destroy(&gen->cond_yield);
        pthread_mutex_destroy(&gen->mtx);
        free(gen);
        return NULL;
    }

    // Create the generator thread
    int rc = pthread_create(&gen->thread_id, NULL, generator_thread_entry, gen);
    if (rc != 0) {
        errno = rc; // pthread_create sets errno on failure
        perror("pthread_create failed");
        pthread_cond_destroy(&gen->cond_next);
        pthread_cond_destroy(&gen->cond_yield);
        pthread_mutex_destroy(&gen->mtx);
        free(gen);
        return NULL;
    }

    // printf("[Main] Created generator %p, thread %p\n", (void*)gen, (void*)gen->thread_id);
    return gen;
}
/**
 * @brief Gets the next value from the generator. Signals the generator thread
 *        to run and waits for it to yield or finish.
 *
 * @param gen The generator object.
 * @param done Output parameter, set to true if the generator finished.
 * @return The yielded value, or the last yielded value if done is true.
 */
static int64_t generator_next(generator_t* gen, bool* done) {
    if (!gen) {
        if (done) *done = true;
        return 0;
    }

    int64_t value = 0;
    bool is_finished = false;

    pthread_mutex_lock(&gen->mtx);
    // printf("[Main] Calling next() for gen %p. Current state: %d\n", (void*)gen, gen->state);

    if (gen->state == GEN_FINISHED) {
        // printf("[Main] Generator %p already finished.\n", (void*)gen);
        is_finished = true;
        value = gen->yielded_value; // Return last value
    } else {
        // Signal the generator thread to run (or start)
        gen->state = GEN_RUNNING; // Indicate intention to run
        gen->value_ready = false; // We need a new value
        // printf("[Main] Signaling generator %p to run (cond_yield).\n", (void*)gen);
        pthread_cond_signal(&gen->cond_yield);

        // Wait until the generator yields a value or finishes
        // printf("[Main] Waiting for generator %p (cond_next)...\n", (void*)gen);
        while (!gen->value_ready && gen->state != GEN_FINISHED) {
            pthread_cond_wait(&gen->cond_next, &gen->mtx);
        }
        // printf("[Main] Woken up for generator %p. State: %d, ValueReady: %d\n", (void*)gen, gen->state, gen->value_ready);


        if (gen->state == GEN_FINISHED) {
            is_finished = true;
            value = gen->yielded_value; // Get value potentially set before finishing
        } else {
            // Generator yielded normally
            is_finished = false;
            value = gen->yielded_value;
            // Important: Keep state as SUSPENDED after yield wakes us
             if (gen->state != GEN_FINISHED) { // Avoid race if it finished while we waited
                 // State should have been set to SUSPENDED by yield() before signaling us
                 // If not, something is wrong, but we proceed based on value_ready
             }
        }
    }

    pthread_mutex_unlock(&gen->mtx);

    if (done) {
        *done = is_finished;
    }
    // printf("[Main] next() returning %lld, done=%s\n", value, is_finished ? "true" : "false");
    return value;
}
/**
 * @brief Destroys the generator, waits for its thread to finish, and cleans up resources.
 *        Ensure the generator has finished (or is signaled to finish) before calling.
 *
 * @param gen The generator object to destroy.
 */
static void generator_destroy(generator_t* gen) {
    if (!gen) return;

    // printf("[Main] Destroying generator %p...\n", (void*)gen);

    pthread_mutex_lock(&gen->mtx);
    bool needs_join = gen->started && gen->state != GEN_FINISHED;
    gen->state = GEN_FINISHED; // Mark as finished
    gen->value_ready = true;   // Ensure any waiting next() call wakes up
    pthread_mutex_unlock(&gen->mtx); // Unlock before signaling/joining

    if (needs_join) {
        // printf("[Main] Generator %p needs join. Signaling cond_yield/cond_next...\n", (void*)gen);
        // Signal potentially waiting threads (in yield or initial wait)
        pthread_cond_signal(&gen->cond_yield);
        // Signal potentially waiting next() call
        pthread_cond_signal(&gen->cond_next);

        // printf("[Main] Joining thread %p...\n", (void*)gen->thread_id);
        pthread_join(gen->thread_id, NULL);
        // printf("[Main] Thread %p joined.\n", (void*)gen->thread_id);
    } else {
         // printf("[Main] Generator %p thread already finished or not started.\n", (void*)gen);
    }


    // Clean up resources
    pthread_mutex_destroy(&gen->mtx);
    pthread_cond_destroy(&gen->cond_yield);
    pthread_cond_destroy(&gen->cond_next);
    free(gen);
    // printf("[Main] Generator %p destroyed.\n", (void*)gen);
}

/**
 * @brief Called from within the generator function (running in the generator thread)
 *        to yield a value back to the caller of generator_next().
 *
 * @param self The generator object (passed to the user function).
 * @param value The value to yield.
 */
static void yield(generator_t* self, int64_t value) {
    if (!self) return;

    pthread_mutex_lock(&self->mtx);
    // printf("[Thread %p] Yielding value %lld.\n", (void*)pthread_self(), value);

    self->yielded_value = value;
    self->value_ready = true;
    self->state = GEN_SUSPENDED; // Mark as suspended *before* signaling

    // Signal the caller waiting in next() that a value is ready
    // printf("[Thread %p] Signaling caller (cond_next).\n", (void*)pthread_self());
    pthread_cond_signal(&self->cond_next);

    // Wait for the caller to call next() again
    // printf("[Thread %p] Waiting for caller signal (cond_yield)...\n", (void*)pthread_self());
    while (self->state == GEN_SUSPENDED) { // Loop protects against spurious wakeups
        pthread_cond_wait(&self->cond_yield, &self->mtx);
         // Check if destroy was called while waiting
         if (self->state == GEN_FINISHED) break;
    }
    // printf("[Thread %p] Woken up by caller. New state: %d\n", (void*)pthread_self(), self->state);

    // If state is now FINISHED (e.g., destroy called), don't proceed further
    bool finished = (self->state == GEN_FINISHED);

    pthread_mutex_unlock(&self->mtx);

    // If finished, exit the thread's context cleanly
    if (finished) {
        // printf("[Thread %p] Exiting due to FINISHED state after yield wait.\n", (void*)pthread_self());
        pthread_exit(NULL);
    }
}

#endif // GENERATOR_PTHREAD_H
