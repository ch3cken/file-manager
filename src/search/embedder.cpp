/**
 * @file embedder.cpp
 * @brief ONNX Runtime-backed sentence embedder.
 *
 * Owner: Antonio (NLP module)
 *
 * Implements nlp::Embedder declared in include/embedder.hpp.
 *
 * NLP pipeline (REQ-4.1.3.5, REQ-2.2.1.2, REQ-3.3.2):
 *   text / file
 *     └─► tokenize()       whitespace tokenisation + BERT vocab lookup
 *           └─► ONNX Run() transformer forward pass (all-MiniLM-L6-v2)
 *                 └─► mean-pool hidden states (if 3-D output)
 *                       └─► l2Normalize()  unit-norm vector for cosine search
 */

#include "embedder.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <onnxruntime_cxx_api.h>

namespace nlp {
namespace {

constexpr const char* kClsToken     = "[CLS]";
constexpr const char* kSepToken     = "[SEP]";
constexpr const char* kPadToken     = "[PAD]";
constexpr const char* kUnknownToken = "[UNK]";

std::int64_t requireTokenId(const std::unordered_map<std::string, std::int64_t>& vocabulary,
                             const std::string& token) {
    const auto it = vocabulary.find(token);
    if (it == vocabulary.end()) {
        throw std::runtime_error("Vocabulary is missing required token: " + token);
    }
    return it->second;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Embedder::Embedder(Options options)
    : options_(std::move(options)),
      environment_(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "filemanager-nlp")),
      sessionOptions_(std::make_unique<Ort::SessionOptions>()) {

    if (options_.modelPath.empty()) {
        throw std::invalid_argument("Embedder requires a non-empty ONNX model path");
    }
    if (options_.vocabPath.empty()) {
        throw std::invalid_argument("Embedder requires a non-empty vocabulary path");
    }
    if (!std::filesystem::exists(options_.modelPath)) {
        throw std::runtime_error("ONNX model file does not exist: " + options_.modelPath.string());
    }
    if (!std::filesystem::exists(options_.vocabPath)) {
        throw std::runtime_error("Vocabulary file does not exist: " + options_.vocabPath.string());
    }
    if (options_.maxSequenceLength < 3) {
        throw std::invalid_argument("maxSequenceLength must be at least 3");
    }

    sessionOptions_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (options_.intraOpThreads > 0) {
        sessionOptions_->SetIntraOpNumThreads(static_cast<int>(options_.intraOpThreads));
    }

    // Windows requires a wide-string path for the ONNX Runtime C++ API.
#ifdef _WIN32
    const std::wstring modelPath = options_.modelPath.wstring();
    session_ = std::make_unique<Ort::Session>(*environment_, modelPath.c_str(), *sessionOptions_);
#else
    const std::string modelPath = options_.modelPath.string();
    session_ = std::make_unique<Ort::Session>(*environment_, modelPath.c_str(), *sessionOptions_);
#endif

    loadVocabulary();

    // Cache input / output names so we don't allocate per-inference call.
    Ort::AllocatorWithDefaultOptions allocator;
    const std::size_t inputCount  = session_->GetInputCount();
    const std::size_t outputCount = session_->GetOutputCount();
    inputNames_.reserve(inputCount);
    outputNames_.reserve(outputCount);

    for (std::size_t i = 0; i < inputCount; ++i) {
        inputNames_.emplace_back(session_->GetInputNameAllocated(i, allocator).get());
    }
    for (std::size_t i = 0; i < outputCount; ++i) {
        outputNames_.emplace_back(session_->GetOutputNameAllocated(i, allocator).get());
    }
    if (inputNames_.empty() || outputNames_.empty()) {
        throw std::runtime_error("ONNX model must expose at least one input and one output");
    }
}

Embedder::~Embedder()                        = default;
Embedder::Embedder(Embedder&&) noexcept      = default;
Embedder& Embedder::operator=(Embedder&&) noexcept = default;

// ---------------------------------------------------------------------------
// Public embedding API
// ---------------------------------------------------------------------------

/**
 * embedText – NLP task: sentence embedding / text vectorisation.
 *
 * Pipeline:
 *   1. Validate and lower-case the input text.
 *   2. Tokenise with BERT vocabulary (whitespace, MVP tokeniser).
 *   3. Build ONNX input tensors: input_ids, attention_mask, token_type_ids.
 *   4. Run the transformer model via ONNX Runtime (local, REQ-3.3.2).
 *   5. Pool output hidden states (CLS or mean-pooling).
 *   6. L2-normalise so dot product == cosine similarity (REQ-4.1.3.5).
 */
std::vector<float> Embedder::embedText(const std::string& text) const {
    if (text.empty()) {
        throw std::invalid_argument("Cannot embed empty text");
    }
    if (!isValidUtf8(text)) {
        throw std::invalid_argument("Cannot embed invalid UTF-8 text");
    }

    const TokenizedInput tokenized = tokenize(text);
    const std::array<std::int64_t, 2> shape{1,
        static_cast<std::int64_t>(tokenized.inputIds.size())};

    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    auto makeTensor = [&](const std::vector<std::int64_t>& values) {
        return Ort::Value::CreateTensor<std::int64_t>(
            memoryInfo,
            const_cast<std::int64_t*>(values.data()),
            values.size(),
            shape.data(),
            shape.size());
    };

    std::vector<Ort::Value>    inputTensors;
    std::vector<const char*>   inputNamePtrs;
    inputTensors.reserve(inputNames_.size());
    inputNamePtrs.reserve(inputNames_.size());

    for (const auto& name : inputNames_) {
        inputNamePtrs.push_back(name.c_str());
        if      (name == "input_ids")      { inputTensors.push_back(makeTensor(tokenized.inputIds)); }
        else if (name == "attention_mask") { inputTensors.push_back(makeTensor(tokenized.attentionMask)); }
        else if (name == "token_type_ids") { inputTensors.push_back(makeTensor(tokenized.tokenTypeIds)); }
        else { throw std::runtime_error("Unsupported ONNX input name: " + name); }
    }

    std::vector<const char*> outputNamePtrs;
    outputNamePtrs.reserve(outputNames_.size());
    for (const auto& name : outputNames_) {
        outputNamePtrs.push_back(name.c_str());
    }

    std::vector<Ort::Value> outputs;
    {
        // Serialise inference to keep behaviour predictable across packaged
        // ONNX Runtime versions on desktop (REQ-5.1.2 background CPU budget).
        std::lock_guard<std::mutex> lock(inferenceMutex_);
        outputs = session_->Run(Ort::RunOptions{nullptr},
                                inputNamePtrs.data(),
                                inputTensors.data(),
                                inputTensors.size(),
                                outputNamePtrs.data(),
                                outputNamePtrs.size());
    }

    if (outputs.empty() || !outputs.front().IsTensor()) {
        throw std::runtime_error("ONNX model did not return a tensor output");
    }

    const auto  tensorInfo = outputs.front().GetTensorTypeAndShapeInfo();
    const auto  dimensions = tensorInfo.GetShape();
    const float* data      = outputs.front().GetTensorData<float>();

    // Shape [1, 384] → CLS-token output (already a sentence vector).
    if (dimensions.size() == 2 &&
        dimensions[0] == 1 &&
        dimensions[1] == kEmbeddingDimension) {
        return l2Normalize(std::vector<float>(data, data + kEmbeddingDimension));
    }

    // Shape [1, seq_len, 384] → mean-pool across attended tokens.
    if (dimensions.size() == 3 &&
        dimensions[0] == 1 &&
        dimensions[2] == kEmbeddingDimension) {

        const std::size_t seqLen = static_cast<std::size_t>(dimensions[1]);
        std::vector<float> pooled(kEmbeddingDimension, 0.0F);
        float tokenCount = 0.0F;

        for (std::size_t t = 0; t < seqLen && t < tokenized.attentionMask.size(); ++t) {
            if (tokenized.attentionMask[t] == 0) { continue; }
            ++tokenCount;
            const float* row = data + t * kEmbeddingDimension;
            for (int d = 0; d < kEmbeddingDimension; ++d) {
                pooled[static_cast<std::size_t>(d)] += row[d];
            }
        }
        if (tokenCount == 0.0F) {
            throw std::runtime_error("ONNX model returned no attended tokens to pool");
        }
        for (float& v : pooled) { v /= tokenCount; }
        return l2Normalize(std::move(pooled));
    }

    std::ostringstream msg;
    msg << "Unexpected ONNX output shape: [";
    for (std::size_t i = 0; i < dimensions.size(); ++i) {
        if (i > 0) { msg << ", "; }
        msg << dimensions[i];
    }
    msg << "]";
    throw std::runtime_error(msg.str());
}

/**
 * embedFile – reads a UTF-8 text file and returns its sentence embedding.
 *
 * Called at index time to produce the BLOB stored in the DB (REQ-2.2.1.2).
 * The returned vector is L2-normalised and ready for cosine comparison.
 */
std::vector<float> Embedder::embedFile(const std::filesystem::path& filePath) const {
    return embedText(readUtf8File(filePath));
}

/**
 * embedTexts – batch helper that reuses one loaded model instance.
 *
 * Preferred over calling embedText() in a loop when indexing many files
 * during the initial scan (REQ-4.3.2.1 step G).
 */
std::vector<std::vector<float>> Embedder::embedTexts(
    const std::vector<std::string>& texts) const {
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());
    for (const auto& text : texts) {
        results.push_back(embedText(text));
    }
    return results;
}

int Embedder::getDimension() const noexcept {
    return kEmbeddingDimension;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Embedder::loadVocabulary() {
    std::ifstream input(options_.vocabPath);
    if (!input) {
        throw std::runtime_error("Failed to open vocabulary file: " +
                                 options_.vocabPath.string());
    }

    std::string token;
    std::int64_t id = 0;
    while (std::getline(input, token)) {
        if (!token.empty() && token.back() == '\r') { token.pop_back(); }
        vocabulary_.emplace(token, id++);
    }
    if (vocabulary_.empty()) {
        throw std::runtime_error("Vocabulary file is empty: " +
                                 options_.vocabPath.string());
    }

    // Verify required BERT special tokens are present.
    (void)requireTokenId(vocabulary_, kClsToken);
    (void)requireTokenId(vocabulary_, kSepToken);
    (void)requireTokenId(vocabulary_, kPadToken);
    (void)requireTokenId(vocabulary_, kUnknownToken);
}

/**
 * tokenize – MVP whitespace tokeniser backed by a BERT vocabulary.
 *
 * NLP task: tokenisation.
 *
 * Sequence format: [CLS] t1 t2 … tN [SEP] [PAD] … [PAD]
 *
 * Note: this is intentionally simple (lowercase + whitespace split).
 * Embedding quality improves when replaced with a proper WordPiece tokeniser.
 * The ONNX execution path, pooling, storage, and search pipeline are correct.
 */
Embedder::TokenizedInput Embedder::tokenize(std::string_view text) const {
    const auto clsId     = requireTokenId(vocabulary_, kClsToken);
    const auto sepId     = requireTokenId(vocabulary_, kSepToken);
    const auto padId     = requireTokenId(vocabulary_, kPadToken);
    const auto unknownId = requireTokenId(vocabulary_, kUnknownToken);

    TokenizedInput result;
    result.inputIds.reserve(options_.maxSequenceLength);
    result.attentionMask.reserve(options_.maxSequenceLength);
    result.tokenTypeIds.reserve(options_.maxSequenceLength);

    // [CLS]
    result.inputIds.push_back(clsId);
    result.attentionMask.push_back(1);
    result.tokenTypeIds.push_back(0);

    // Content tokens
    std::istringstream stream(toLowerAscii(text));
    std::string tok;
    while (stream >> tok &&
           result.inputIds.size() + 1 < options_.maxSequenceLength) {
        const auto it = vocabulary_.find(tok);
        result.inputIds.push_back(it == vocabulary_.end() ? unknownId : it->second);
        result.attentionMask.push_back(1);
        result.tokenTypeIds.push_back(0);
    }

    // [SEP]
    result.inputIds.push_back(sepId);
    result.attentionMask.push_back(1);
    result.tokenTypeIds.push_back(0);

    // Pad to maxSequenceLength
    while (result.inputIds.size() < options_.maxSequenceLength) {
        result.inputIds.push_back(padId);
        result.attentionMask.push_back(0);
        result.tokenTypeIds.push_back(0);
    }
    return result;
}

bool Embedder::isValidUtf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t cont = 0;

        if      ((lead & 0x80U) == 0x00U) { ++i; continue; }
        else if ((lead & 0xE0U) == 0xC0U) { cont = 1; if (lead < 0xC2U) return false; }
        else if ((lead & 0xF0U) == 0xE0U) { cont = 2; }
        else if ((lead & 0xF8U) == 0xF0U) { cont = 3; if (lead > 0xF4U) return false; }
        else { return false; }

        if (i + cont >= text.size()) { return false; }
        for (std::size_t k = 1; k <= cont; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) { return false; }
        }
        i += cont + 1;
    }
    return true;
}

std::string Embedder::readUtf8File(const std::filesystem::path& filePath) {
    if (filePath.empty()) {
        throw std::invalid_argument("Cannot embed a file with an empty path");
    }
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file for embedding: " + filePath.string());
    }
    std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("Failed while reading file: " + filePath.string());
    }
    if (contents.empty()) {
        throw std::invalid_argument("Cannot embed empty file: " + filePath.string());
    }
    if (!isValidUtf8(contents)) {
        throw std::invalid_argument("File is not valid UTF-8 text: " + filePath.string());
    }
    return contents;
}

std::string Embedder::toLowerAscii(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

/**
 * l2Normalize – NLP task: vector normalisation for cosine similarity.
 *
 * After normalisation, cosine(a, b) == dot(a, b).
 * This is required so VectorSearch can use a plain dot-product (Eigen)
 * without dividing by magnitudes at query time (REQ-4.1.3.5).
 */
std::vector<float> Embedder::l2Normalize(std::vector<float> values) {
    const double normSq = std::inner_product(
        values.begin(), values.end(), values.begin(), 0.0);
    if (normSq <= 0.0) {
        throw std::runtime_error("Cannot normalise a zero embedding vector");
    }
    const float invNorm = static_cast<float>(1.0 / std::sqrt(normSq));
    for (float& v : values) { v *= invNorm; }
    return values;
}

} // namespace nlp
