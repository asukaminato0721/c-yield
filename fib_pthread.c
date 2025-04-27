#include "generator_pthread.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// User-defined Fibonacci generator function
// Note: It receives a generator_t* pointer
void fib_generator_func(generator_t* self)
{
    int64_t a = 1;
    int64_t b = 1;

    // Generate the first 10 Fibonacci numbers
    for (size_t i = 0; i < 10; ++i) {
        // Use the wrapped yield function
        yield(self, a);

        // Calculate the next number
        int64_t next_a = b;
        int64_t next_b = a + b;
        a = next_a;
        b = next_b;

        // Simple overflow check (optional)
        if (a < 0 || b < 0) {
            fprintf(stderr, "[Fib Generator] Overflow detected.\n");
            break; // Exit the loop early, the function will return
        }
    }

    // When the function finishes execution and returns, the generator state will become FINISHED
    printf("[Fib Generator] Function finished.\n");
}

int32_t main()
{
    printf("Creating Fibonacci generator...\n");
    // Create the generator, using the default stack size (pass 0) or a specified size
    generator_t* fib_gen = generator_create(fib_generator_func, NULL);
    if (!fib_gen) {
        return 1;
    }

    printf("Generating Fibonacci numbers using the generator wrapper:\n");

    bool finished = false;
    size_t count = 0;
    while (!finished && count < 15) { // Add a maximum count just in case
        int64_t value = generator_next(fib_gen, &finished);
        if (!finished) {
            printf("%" PRId64 "\n", value);
        } else {
            printf("Generator finished.\n");
        }
        count++;
    }

    // Check if stopped due to reaching the maximum count
    if (count >= 15 && !finished) {
        printf("Stopped after reaching max count.\n");
    }

    printf("Destroying generator...\n");
    generator_destroy(fib_gen);

    printf("Finished.\n");
    return EXIT_SUCCESS;
}
