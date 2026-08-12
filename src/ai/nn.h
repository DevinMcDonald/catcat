#pragma once
#include <cstddef>
#include <string>
#include <vector>

// Minimal 3-layer feedforward network (in → h1 → h2 → out).
// Activations: ReLU on hidden layers, linear output (caller applies softmax).
// All parameters stored as a flat float vector for easy evolutionary
// perturbation.
struct NeuralNet {
    int n_in, n_h1, n_h2, n_out;

    // Flat parameter vector: [W1 | b1 | W2 | b2 | W3 | b3]
    std::vector<float> params;

    // Construct with Xavier-initialised random weights.
    NeuralNet(int n_in, int n_h1, int n_h2, int n_out);

    // Forward pass — returns raw logits (length n_out).
    std::vector<float> Forward(const std::vector<float> &input) const;

    // Return a copy with Gaussian noise added to every parameter.
    NeuralNet Perturbed(float sigma) const;

    // Persist to / restore from a binary file.
    // Returns false on error (missing file, size mismatch, etc.).
    bool Save(const std::string &path) const;
    bool Load(const std::string &path);

    std::size_t NumParams() const { return params.size(); }

  private:
    // Offsets into params[] for each weight block.
    int off_w1() const { return 0; }
    int off_b1() const { return n_in * n_h1; }
    int off_w2() const { return off_b1() + n_h1; }
    int off_b2() const { return off_w2() + n_h1 * n_h2; }
    int off_w3() const { return off_b2() + n_h2; }
    int off_b3() const { return off_w3() + n_h2 * n_out; }
    int total() const { return off_b3() + n_out; }
};
