/**
 * @file test_embedder.cpp
 * @brief Unit-style tests for nlp::Embedder.
 *
 * Owner: Antonio (NLP module)
 *
 * These tests verify the Embedder interface at compile time and document
 * expected runtime behaviour. Runtime inference tests require a real model
 * file and are guarded by FILEMANAGER_RUN_INFERENCE_TESTS.
 *
 * SRS requirements exercised:
 *   REQ-4.1.3.5  – embedText() produces a vector that can be used for cosine
 *                  similarity (unit-norm guarantee).
 *   REQ-2.2.1.2  – embedFile() is the indexing-time API.
 *   REQ-3.3.2    – local ONNX Runtime execution; no network access.
 *   REQ-2.4.2    – private: no data leaves the device.
 */

#include "embedder.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool floatNear(float a, float b, float tol = 1e-5F) {
    return std::abs(a - b) <= tol;
}

/** Compute the L2 norm of a vector. */
static float l2Norm(const std::vector<float>& v) {
    float sum = 0.0F;
    for (float x : v) { sum += x * x; }
    return std::sqrt(sum);
}

/** Compute dot product (== cosine similarity for unit-norm vectors). */
static float dotProduct(const std::vector<float>& a, const std::vector<float>& b) {
    assert(a.size() == b.size());
    float result = 0.0F;
    for (std::size_t i = 0; i < a.size(); ++i) { result += a[i] * b[i]; }
    return result;
}

/** Write a temporary UTF-8 text file; caller deletes it. */
static std::filesystem::path writeTempFile(const std::string& content,
                                           const std::string& name = "tmp_embed_test.txt") {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << content;
    return path;
}

// ---------------------------------------------------------------------------
// Compile-time checks (REQ-4.1.3.5)
// ---------------------------------------------------------------------------

static void testCompileTimeChecks() {
    // Dimension must be 384 for all-MiniLM-L6-v2.
    static_assert(nlp::Embedder::kEmbeddingDimension == 384,
                  "REQ-4.1.3.5: embedding dimension must be 384");

    // Options struct must be constructible with paths.
    [[maybe_unused]] nlp::Embedder::Options opts;
    opts.modelPath        = "models/all-MiniLM-L6-v2.onnx";
    opts.vocabPath        = "models/vocab.txt";
    opts.maxSequenceLength = 256;
    opts.intraOpThreads   = 0;

    std::cout << "[PASS] compile-time dimension check\n";
}

// ---------------------------------------------------------------------------
// Input validation tests (no model required)
// ---------------------------------------------------------------------------

static void testMissingModelPathThrows() {
    nlp::Embedder::Options opts;
    opts.modelPath = "";
    opts.vocabPath = "models/vocab.txt";
    try {
        nlp::Embedder e(opts);
        assert(false && "Expected std::invalid_argument for empty model path");
    } catch (const std::invalid_argument&) {
        std::cout << "[PASS] empty model path throws invalid_argument\n";
    }
}

static void testMissingVocabPathThrows() {
    nlp::Embedder::Options opts;
    opts.modelPath = "models/all-MiniLM-L6-v2.onnx"; // doesn't have to exist for this check
    opts.vocabPath = "";
    try {
        nlp::Embedder e(opts);
        assert(false && "Expected std::invalid_argument for empty vocab path");
    } catch (const std::invalid_argument&) {
        std::cout << "[PASS] empty vocab path throws invalid_argument\n";
    }
}

static void testNonExistentModelThrows() {
    nlp::Embedder::Options opts;
    opts.modelPath = "/does/not/exist.onnx";
    opts.vocabPath = "/does/not/exist.txt";
    try {
        nlp::Embedder e(opts);
        assert(false && "Expected std::runtime_error for missing model file");
    } catch (const std::runtime_error&) {
        std::cout << "[PASS] missing model file throws runtime_error\n";
    }
}

static void testTooShortMaxSequenceLengthThrows() {
    nlp::Embedder::Options opts;
    opts.modelPath        = "models/all-MiniLM-L6-v2.onnx";
    opts.vocabPath        = "models/vocab.txt";
    opts.maxSequenceLength = 2; // must be >= 3
    try {
        nlp::Embedder e(opts);
        assert(false && "Expected std::invalid_argument for maxSequenceLength < 3");
    } catch (const std::invalid_argument&) {
        std::cout << "[PASS] maxSequenceLength < 3 throws invalid_argument\n";
    }
}

// ---------------------------------------------------------------------------
// Runtime inference tests (require model files)
// REQ-4.1.3.5, REQ-2.2.1.2
// ---------------------------------------------------------------------------

#ifdef FILEMANAGER_RUN_INFERENCE_TESTS

static void testEmbedTextReturnsDimension(const nlp::Embedder& embedder) {
    // REQ-4.1.3.5: vector must have exactly kEmbeddingDimension elements.
    const auto vec = embedder.embedText("machine learning paper");
    assert(static_cast<int>(vec.size()) == nlp::Embedder::kEmbeddingDimension);
    std::cout << "[PASS] embedText returns correct dimension\n";
}

static void testEmbedTextIsUnitNorm(const nlp::Embedder& embedder) {
    // REQ-4.1.3.5: output must be L2-normalised (|v| == 1.0).
    // When unit-norm, dot(a,b) == cosine(a,b) – no extra normalisation at
    // query time.
    const auto vec = embedder.embedText("photo from last week");
    const float norm = l2Norm(vec);
    assert(floatNear(norm, 1.0F, 1e-4F) && "Embedding must be unit-norm");
    std::cout << "[PASS] embedText output is unit-norm (norm=" << norm << ")\n";
}

static void testSemanticallySimilarTextsHigherSimilarity(const nlp::Embedder& embedder) {
    // REQ-4.1.3.5: semantically related texts should score higher than
    // unrelated texts.
    const auto vecA = embedder.embedText("machine learning neural network");
    const auto vecB = embedder.embedText("deep learning artificial intelligence");
    const auto vecC = embedder.embedText("cooking recipe pasta dinner");

    const float simAB = dotProduct(vecA, vecB);
    const float simAC = dotProduct(vecA, vecC);

    assert(simAB > simAC && "Related texts must have higher cosine similarity");
    std::cout << "[PASS] semantic similarity: related=" << simAB
              << " unrelated=" << simAC << "\n";
}

static void testEmbedTextsMatchEmbedText(const nlp::Embedder& embedder) {
    // embedTexts() must produce identical results to individual embedText() calls.
    const std::vector<std::string> texts = {
        "budget document yesterday",
        "vacation photo last week",
    };
    const auto batch = embedder.embedTexts(texts);
    for (std::size_t i = 0; i < texts.size(); ++i) {
        const auto single = embedder.embedText(texts[i]);
        assert(single.size() == batch[i].size());
        for (std::size_t d = 0; d < single.size(); ++d) {
            assert(floatNear(single[d], batch[i][d], 1e-5F));
        }
    }
    std::cout << "[PASS] embedTexts matches individual embedText calls\n";
}

static void testEmbedFileMatchesEmbedText(const nlp::Embedder& embedder) {
    // REQ-2.2.1.2: embedding a file must match embedding its contents as text.
    const std::string content = "software engineering paper downloaded a week ago";
    const auto tmpPath = writeTempFile(content);

    const auto fromText = embedder.embedText(content);
    const auto fromFile = embedder.embedFile(tmpPath);

    assert(fromText.size() == fromFile.size());
    for (std::size_t d = 0; d < fromText.size(); ++d) {
        assert(floatNear(fromText[d], fromFile[d], 1e-5F));
    }
    std::filesystem::remove(tmpPath);
    std::cout << "[PASS] embedFile matches embedText for same content\n";
}

static void testEmptyTextThrows(const nlp::Embedder& embedder) {
    try {
        embedder.embedText("");
        assert(false && "Expected std::invalid_argument for empty text");
    } catch (const std::invalid_argument&) {
        std::cout << "[PASS] empty text throws invalid_argument\n";
    }
}

static void testEmptyFileThrows(const nlp::Embedder& embedder) {
    const auto tmpPath = writeTempFile("", "empty_test.txt");
    try {
        embedder.embedFile(tmpPath);
        assert(false && "Expected std::invalid_argument for empty file");
    } catch (const std::invalid_argument&) {
        std::cout << "[PASS] empty file throws invalid_argument\n";
    }
    std::filesystem::remove(tmpPath);
}

static void testGetDimension(const nlp::Embedder& embedder) {
    assert(embedder.getDimension() == nlp::Embedder::kEmbeddingDimension);
    std::cout << "[PASS] getDimension returns " << embedder.getDimension() << "\n";
}

static void runInferenceTests(const std::string& modelPath,
                               const std::string& vocabPath) {
    nlp::Embedder::Options opts;
    opts.modelPath = modelPath;
    opts.vocabPath = vocabPath;
    nlp::Embedder embedder(opts);

    testGetDimension(embedder);
    testEmbedTextReturnsDimension(embedder);
    testEmbedTextIsUnitNorm(embedder);
    testSemanticallySimilarTextsHigherSimilarity(embedder);
    testEmbedTextsMatchEmbedText(embedder);
    testEmbedFileMatchesEmbedText(embedder);
    testEmptyTextThrows(embedder);
    testEmptyFileThrows(embedder);
}

#endif // FILEMANAGER_RUN_INFERENCE_TESTS

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::cout << "=== nlp::Embedder tests ===\n";

    // Always-on: compile-time and input-validation tests.
    testCompileTimeChecks();
    testMissingModelPathThrows();
    testMissingVocabPathThrows();
    testNonExistentModelThrows();
    testTooShortMaxSequenceLengthThrows();

#ifdef FILEMANAGER_RUN_INFERENCE_TESTS
    // Inference tests: pass model and vocab paths as arguments.
    // Usage: ./test_embedder <model.onnx> <vocab.txt>
    if (argc < 3) {
        std::cerr << "Inference tests require two arguments: "
                     "<model.onnx> <vocab.txt>\n";
        return 1;
    }
    runInferenceTests(argv[1], argv[2]);
#else
    (void)argc; (void)argv;
    std::cout << "[INFO] Inference tests skipped "
                 "(define FILEMANAGER_RUN_INFERENCE_TESTS to enable)\n";
#endif

    std::cout << "=== All tests passed ===\n";
    return 0;
}
