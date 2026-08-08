#include "ai/nn.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

NeuralNet::NeuralNet(int n_in_, int n_h1_, int n_h2_, int n_out_)
    : n_in(n_in_), n_h1(n_h1_), n_h2(n_h2_), n_out(n_out_) {
    params.resize(static_cast<std::size_t>(total()), 0.0f);

    // Xavier initialisation: scale = sqrt(2 / fan_in)
    std::mt19937 rng(std::random_device{}());
    auto xavier = [&](int fan_in) {
        const float scale = std::sqrt(2.0f / static_cast<float>(fan_in));
        return std::normal_distribution<float>(0.0f, scale);
    };

    auto fill = [&](int offset, int count, std::normal_distribution<float>& dist) {
        for (int i = 0; i < count; ++i) {
            params[static_cast<std::size_t>(offset + i)] = dist(rng);
        }
    };

    auto d1 = xavier(n_in);  fill(off_w1(), n_in  * n_h1,  d1);
    auto d2 = xavier(n_h1);  fill(off_w2(), n_h1  * n_h2,  d2);
    auto d3 = xavier(n_h2);  fill(off_w3(), n_h2  * n_out, d3);
    // biases stay at zero
}

std::vector<float> NeuralNet::Forward(const std::vector<float>& in) const {
    const float* w1 = params.data() + off_w1();
    const float* b1 = params.data() + off_b1();
    const float* w2 = params.data() + off_w2();
    const float* b2 = params.data() + off_b2();
    const float* w3 = params.data() + off_w3();
    const float* b3 = params.data() + off_b3();

    // Layer 1: h1 = ReLU(W1*in + b1)
    std::vector<float> h1(static_cast<std::size_t>(n_h1));
    for (int j = 0; j < n_h1; ++j) {
        float s = b1[j];
        for (int i = 0; i < n_in; ++i) {
            s += w1[j * n_in + i] * in[static_cast<std::size_t>(i)];
        }
        h1[static_cast<std::size_t>(j)] = std::max(0.0f, s);
    }

    // Layer 2: h2 = ReLU(W2*h1 + b2)
    std::vector<float> h2(static_cast<std::size_t>(n_h2));
    for (int j = 0; j < n_h2; ++j) {
        float s = b2[j];
        for (int i = 0; i < n_h1; ++i) {
            s += w2[j * n_h1 + i] * h1[static_cast<std::size_t>(i)];
        }
        h2[static_cast<std::size_t>(j)] = std::max(0.0f, s);
    }

    // Layer 3: out = W3*h2 + b3  (logits, no activation)
    std::vector<float> out(static_cast<std::size_t>(n_out));
    for (int j = 0; j < n_out; ++j) {
        float s = b3[j];
        for (int i = 0; i < n_h2; ++i) {
            s += w3[j * n_h2 + i] * h2[static_cast<std::size_t>(i)];
        }
        out[static_cast<std::size_t>(j)] = s;
    }
    return out;
}

NeuralNet NeuralNet::Perturbed(float sigma) const {
    NeuralNet copy = *this;
    std::mt19937 rng(std::random_device{}());
    std::normal_distribution<float> dist(0.0f, sigma);
    for (auto& p : copy.params) {
        p += dist(rng);
    }
    return copy;
}

bool NeuralNet::Save(const std::string& path) const {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    // Header: 4 ints
    const int hdr[4] = {n_in, n_h1, n_h2, n_out};
    f.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(params.data()),
            static_cast<std::streamsize>(params.size() * sizeof(float)));
    return f.good();
}

bool NeuralNet::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    int hdr[4];
    f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (!f || hdr[0] != n_in || hdr[1] != n_h1 ||
        hdr[2] != n_h2 || hdr[3] != n_out) {
        return false;
    }
    f.read(reinterpret_cast<char*>(params.data()),
           static_cast<std::streamsize>(params.size() * sizeof(float)));
    return f.good();
}
