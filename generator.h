#ifndef GENERATOR_H
#define GENERATOR_H
#include <stdio.h>
#include <stdlib.h>
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE // For ucontext
#endif
#include <stdbool.h>
#include <stdint.h> // For int64_t
#include <ucontext.h>

// --- Constants ---
#define DEFAULT_STACK_SIZE (16 * 1024) // Default 16KB stack

// --- Opaque Generator Type ---

// User should not access the internal structure directly
typedef struct generator generator_t;

// --- Generator Function Type ---

// The generator function provided by the user must conform to this signature
// It receives a pointer to its own generator object
typedef void (*generator_func_t)(generator_t* self);

typedef enum { GEN_RUNNING,
    GEN_SUSPENDED,
    GEN_FINISHED } generator_state_t;

struct generator {
    ucontext_t context; // Generator's own context
    ucontext_t caller_context; // Context of the caller of generator_next
    void* stack; // Stack allocated for the generator
    size_t stack_size; // Stack size
    generator_func_t user_func; // User-provided function
    int64_t yielded_value; // The currently yielded value
    generator_state_t state; // State of the generator
    void* user_data;
};

// --- Private Helper Function ---

// This is the actual entry point function passed to makecontext.
// It is responsible for calling the user-provided generator function
// and handling the state after the user function returns.
static void generator_entry_point(void* arg)
{
    generator_t* self = (generator_t*)arg;

    // Call the user-provided generator function
    self->user_func(self);

    // If user_func returns, it means the generator has finished
    self->state = GEN_FINISHED;

    // printf("[Generator %p] User function finished. Swapping back to caller.\n",
    // (void*)self); Last switch back to the caller (generator_next)
    if (swapcontext(&self->context, &self->caller_context) == -1) {
        perror("swapcontext (generator finish -> caller) failed");
        // Difficult to recover from this
    }
    // Control should not return here
}

// --- Public API Implementation ---

/**
 * @brief Creates a new generator.
 *
 * @param func The user-provided generator function.
 * @param user_data Optional user data to pass to the generator function.
 * @param stack_size The stack size (in bytes) for the generator coroutine.
 * Recommended at least 16KB.
 * @return A pointer to the new generator on success, or NULL on failure.
 */
static generator_t* generator_create(generator_func_t func, void* user_data,
    size_t stack_size)
{
    if (!func) {
        fprintf(stderr, "Error: Generator function cannot be NULL.\n");
        return NULL;
    }

    generator_t* gen = (generator_t*)malloc(sizeof(generator_t));
    if (!gen) {
        perror("malloc for generator_t failed");
        return NULL;
    }

    gen->stack_size = (stack_size > 0) ? stack_size : DEFAULT_STACK_SIZE;
    gen->stack = malloc(gen->stack_size);
    if (!gen->stack) {
        perror("malloc for generator stack failed");
        free(gen);
        return NULL;
    }

    gen->user_func = func;
    gen->state = GEN_SUSPENDED;
    gen->yielded_value = 0;
    gen->user_data = user_data;

    if (getcontext(&gen->context) == -1) {
        perror("getcontext for generator failed");
        free(gen->stack);
        free(gen);
        return NULL;
    }

    gen->context.uc_stack.ss_sp = gen->stack;
    gen->context.uc_stack.ss_size = gen->stack_size;
    gen->context.uc_link = &gen->caller_context;

    makecontext(&gen->context, (void (*)(void))generator_entry_point, 1, gen);

    return gen;
}

/**
 * @brief Gets the next value from the generator.
 *
 * @param gen Pointer to the generator to operate on.
 * @param done Output parameter. Set to true if the generator has finished
 * (function returned), false otherwise.
 * @return The yielded value if the generator is not finished. If the generator
 * is finished, the return value is undefined (often 0 or the last yielded
 * value, rely on the done flag).
 */
static int64_t generator_next(generator_t* gen, bool* done)
{
    if (!gen) {
        if (done)
            *done = true;
        return 0;
    }

    if (gen->state == GEN_FINISHED) {
        if (done)
            *done = true;
        return gen->yielded_value;
    }

    gen->state = GEN_RUNNING;
    if (swapcontext(&gen->caller_context, &gen->context) == -1) {
        perror("swapcontext (caller -> generator) failed");
        gen->state = GEN_FINISHED;
        if (done)
            *done = true;
        return 0;
    }

    if (done) {
        *done = (gen->state == GEN_FINISHED);
    }

    return gen->yielded_value;
}

/**
 * @brief Called from within the generator function to suspend the generator and
 * return a value to the caller.
 *        **Note: This function should only be called by the user-provided
 * generator function.**
 *
 * @param self Pointer to the currently executing generator object (received by
 * the generator function).
 * @param value The value to yield.
 */
static void yield(generator_t* self, int64_t value)
{
    if (!self || self->state != GEN_RUNNING) {
        fprintf(stderr, "Error: yield() called outside of a running generator "
                        "context or with invalid generator.\n");
        return;
    }

    self->yielded_value = value;
    self->state = GEN_SUSPENDED;

    if (swapcontext(&self->context, &self->caller_context) == -1) {
        perror("swapcontext (yield -> caller) failed");
        self->state = GEN_FINISHED;
    }
}

/**
 * @brief Destroys the generator and releases its resources (including the
 * stack).
 *
 * @param gen Pointer to the generator to destroy.
 */
static void generator_destroy(generator_t* gen)
{
    if (gen) {
        if (gen->stack) {
            free(gen->stack);
        }
        free(gen);
    }
}

#endif // GENERATOR_H
