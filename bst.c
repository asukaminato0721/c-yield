#include "generator.h"
#include <assert.h>
#include <inttypes.h> // For PRId64 macro
#include <stdbool.h>
#include <stdint.h> // Include for int64_t if not already
#include <stdio.h>
#include <stdlib.h>

typedef struct TreeNode {
    int32_t data;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;

// --- BST Helper Functions ---
TreeNode* create_node(int32_t data)
{
    TreeNode* newNode = malloc(sizeof(TreeNode));
    if (!newNode) {
        perror("Failed to allocate TreeNode");
        exit(EXIT_FAILURE);
    }
    newNode->data = data;
    newNode->left = NULL;
    newNode->right = NULL;
    return newNode;
}

// Free the tree (post-order traversal)
void free_tree(TreeNode* node)
{
    if (node == NULL) {
        return;
    }
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}

// --- Recursive Helper for In-order Traversal ---
// This function performs the actual recursion and yielding
void inorder_recursive_helper(generator_t* self, TreeNode* node)
{
    if (node == NULL) {
        return;
    }

    // Simulate "yield from f(root.left)"
    inorder_recursive_helper(self, node->left);

    // Check generator state before yielding - important if yield fails
    if (self->state != GEN_RUNNING)
        return;

    // Simulate "yield root.value"
    // printf("[Generator %p, Node %d] Yielding %d\n", (void*)self, node->data,
    // node->data);
    yield(self, (int64_t)node->data);
    // After yield, control might return here if generator_next is called again

    // Check generator state again after yielding
    if (self->state != GEN_RUNNING)
        return;

    // Simulate "yield from f(root.right)"
    inorder_recursive_helper(self, node->right);
}

// --- Main Generator Function (Entry Point) ---
// This is the function passed to generator_create
void bst_inorder_recursive_generator(generator_t* self)
{
    TreeNode* root = self->user_data;
    // printf("[Generator %p] Starting recursive traversal from root %p...\n",
    // (void*)self, (void*)root);
    inorder_recursive_helper(self, root);
    // printf("[Generator %p] Recursive traversal function finished.\n",
    // (void*)self); When inorder_recursive_helper returns, this function also
    // returns, causing the state to become GEN_FINISHED in generator_entry_point.
}

// In the same file (bst_recursive_example.c)

// Function to check the BST property using two generators
bool check_bst_property(TreeNode* root)
{
    printf("\n--- Checking BST Property ---\n");
    if (!root) {
        printf("Empty tree, property holds.\n");
        return true; // Empty tree is considered valid
    }

    // Create two independent generators for the same tree
    // Use a larger stack size if deep recursion is expected
    size_t stack_size = 32 * 1024; // 32 KB, adjust as needed
    generator_t* gen_a = generator_create(bst_inorder_recursive_generator, root, stack_size);
    generator_t* gen_b = generator_create(bst_inorder_recursive_generator, root, stack_size);

    if (!gen_a || !gen_b) {
        fprintf(stderr, "Failed to create generators.\n");
        if (gen_a)
            generator_destroy(gen_a);
        if (gen_b)
            generator_destroy(gen_b);
        return false; // Indicate failure
    }

    bool finished_a = false;
    bool finished_b = false;
    int64_t value_a, value_b;
    bool result = true; // Assume true initially

    // Advance gen_a once (like next(fa))
    printf("Advancing generator A once...\n");
    value_a = generator_next(gen_a, &finished_a);
    if (finished_a) {
        printf("Tree has 0 or 1 node. Property holds.\n");
        generator_destroy(gen_a);
        generator_destroy(gen_b);
        return true; // Tree with 0 or 1 node is sorted
    }
    printf("Generator A first value: %" PRId64 "\n", value_a);

    printf("Starting simultaneous iteration (like zip)...\n");
    int32_t step = 0;
    while (true) {
        // Get next value from gen_b (current value, like sl)
        value_b = generator_next(gen_b, &finished_b);
        value_a = generator_next(gen_a, &finished_a);

        if (finished_b || finished_a) {
            printf("One of the generators finished. All comparisons passed.\n");
            break;
        }

        printf("Step %d: Comparing A=%" PRId64 " (next) with B=%" PRId64
               " (current)\n",
            step, value_a, value_b);
        if (value_a <= value_b) {
            printf("Check FAILED: %" PRId64 " <= %" PRId64
                   ". Not strictly increasing.\n",
                value_a, value_b);
            result = false;
            break; // Property violated
        } else {
            printf("Check OK: %" PRId64 " > %" PRId64 "\n", value_a, value_b);
        }
        step++;
        if (step > 100) { // Safety break for unexpected issues
            fprintf(stderr, "Error: Safety break triggered in comparison loop.\n");
            result = false;
            break;
        }
    }

    printf("Cleaning up generators...\n");
    generator_destroy(gen_a);
    generator_destroy(gen_b);
    printf("--- Check Finished (Result: %s) ---\n", result ? "true" : "false");
    return result;
}

int32_t main()
{
    printf("Building valid BST...\n");
    TreeNode* root = create_node(50);
    root->left = create_node(30);
    root->right = create_node(70);
    root->left->right = create_node(40);

    assert(check_bst_property(root));
    free_tree(root);
    root = NULL;

    printf("\n=========================\n");

    printf("\nBuilding invalid tree (manual violation)...\n");
    root = create_node(50);
    root->left = create_node(30);
    root->right = create_node(70);
    root->left->right = create_node(60); // Invalid: 60 should be <= 50

    // Check the invalid BST
    // In-order traversal: 30, 60, 50, 70 -> Fails at 60 > 50 check
    assert(!check_bst_property(root));
    free_tree(root);
    root = NULL;

    printf("\nExample finished.\n");
    return 0;
}
