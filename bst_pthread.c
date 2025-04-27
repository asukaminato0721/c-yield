#include "generator_pthread.h" // Use the pthread version
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TreeNode {
    int32_t data;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;

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

// This function performs the actual recursion and yielding
void inorder_recursive_helper(generator_t* self, TreeNode* node)
{
    if (node == NULL) {
        return;
    }
    inorder_recursive_helper(self, node->left);

    // Check if generator was destroyed while recursing left
    pthread_mutex_lock(&self->mtx);
    bool finished = (self->state == GEN_FINISHED);
    pthread_mutex_unlock(&self->mtx);
    if (finished)
        return;

    yield(self, (int64_t)node->data);

    // Check if generator was destroyed while yielded or recursing left
    pthread_mutex_lock(&self->mtx);
    finished = (self->state == GEN_FINISHED);
    pthread_mutex_unlock(&self->mtx);
    if (finished)
        return;

    inorder_recursive_helper(self, node->right);
}

// --- Main Generator Function (Entry Point - SAME AS BEFORE) ---
void bst_inorder_recursive_generator(generator_t* self)
{
    TreeNode* root = (TreeNode*)self->user_data;
    inorder_recursive_helper(self, root);
}

// --- Function to check the BST property (SAME LOGIC AS BEFORE) ---
bool check_bst_property(TreeNode* root)
{
    printf("\n--- Checking BST Property (pthread) ---\n");
    if (!root) {
        printf("Empty tree, property holds.\n");
        return true;
    }

    // Create two independent generators
    // No stack size needed for pthread create here
    generator_t* gen_a = generator_create(bst_inorder_recursive_generator, root);
    generator_t* gen_b = generator_create(bst_inorder_recursive_generator, root);

    if (!gen_a || !gen_b) { /* ... error handling ... */
        return false;
    }

    bool finished_a = false;
    bool finished_b = false;
    int64_t value_a, value_b;
    bool result = true;

    // Advance gen_a once
    printf("Advancing generator A once...\n");
    value_a = generator_next(gen_a, &finished_a);
    if (finished_a) { /* ... handle 0/1 node case ... */
        return true;
    }
    printf("Generator A first value: %" PRId64 "\n", value_a);

    printf("Starting simultaneous iteration (like zip)...\n");
    size_t step = 0;
    while (true) {
        value_a = generator_next(gen_a, &finished_a);
        value_b = generator_next(gen_b, &finished_b);
        if (finished_b || finished_a) { /* ... success ... */
            result = true;
            break;
        }

        printf("Step %" PRId64 ": Comparing A=%" PRId64 " (next) with B=%" PRId64 " (current)\n", step, value_a, value_b);
        if (value_a <= value_b) { /* ... failure ... */
            result = false;
            break;
        } else {
            printf("Check OK: %" PRId64 " > %" PRId64 "\n", value_a, value_b);
        }
        step++;
        if (step > 100) { /* ... safety break ... */
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

// --- Main Function (SAME AS BEFORE) ---
int32_t main()
{
    // Build valid BST
    printf("Building valid BST...\n");
    TreeNode* root = create_node(50);
    root->left = create_node(30);
    root->right = create_node(70);
    root->left->right = create_node(40);

    assert(check_bst_property(root));

    free_tree(root);
    root = NULL;

    printf("\n=========================\n");

    // Build invalid tree
    printf("\nBuilding invalid tree (manual violation)...\n");
    root = create_node(50);
    root->left = create_node(30);
    root->right = create_node(70);
    root->left->right = create_node(60); // Invalid
    assert(!check_bst_property(root));
    free_tree(root);
    root = NULL;
    return EXIT_SUCCESS;
}
