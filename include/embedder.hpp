#ifndef FILEMANAGER_EMBEDDER_HPP
#define FILEMANAGER_EMBEDDER_HPP

#include <filesystem>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward-declare ONNX Runtime types so this header stays lean.
namespace Ort {
class Env;
class Session;
class SessionOptions;
}

namespace nlp {

/**
 * @brief Sentence-embedding engine backed by ONNX Runtime.
 *
 * Targets sentence-transformers/all-MiniLM-L6-v2 and produces 384-dimensional
 * L2-normalised vectors. Each vector can be used directly as a cosine-similarity
 * key: because outputs are unit-norm, cosine(a, b) == dot(a, b).
 *
 * SRS coverage
 * ------------
 * REQ-4.1.3.5  [Semantic-Based Search]   – embedText() vectorises the user query;
 *                                          embedFile() vectorises stored files.
 * REQ-2.2.1.2  [Smart search / indexing] – embedFile() is called at index time to
 *                                          produce the BLOB stored in the DB.
 * REQ-3.3.2    [Local NLP Engine]        – execution runs locally via ONNX Runtime;
 *                                          no external network calls are made.
 * REQ-2.4.2    [Privacy]                 – all inference stays on-device.
 */
class Embedder {
public:
    /** Output dimension for all-MiniLM-L6-v2. */
    static constexpr int kEmbeddingDimension = 384;

    /** Configuration required to initialise the embedder. */
    struct Options {
        std::filesystem::path modelPath;        ///< Path to the .onnx model file.
        std::filesystem::path vocabPath;        ///< Path to vocab.txt (BERT vocabulary).
        std::size_t           maxSequenceLength = 256; ///< Maximum token count including [CLS]/[SEP].
        std::size_t           intraOpThreads    = 0;   ///< 0 = let ONNX Runtime decide.
    };

    /**
     * Loads the ONNX model and vocabulary. Throws on any missing or invalid file.
     *
     * @throws std::invalid_argument  if required paths are empty or maxSequenceLength < 3.
     * @throws std::runtime_error     if the model or vocabulary cannot be loaded.
     */
    explicit Embedder(Options options);
    ~Embedder();

    Embedder(const Embedder&)            = delete;
    Embedder& operator=(const Embedder&) = delete;
    Embedder(Embedder&&) noexcept;
    Embedder& operator=(Embedder&&) noexcept;

    /**
     * Embeds one UTF-8 text string and returns a 384-dimensional L2-normalised vector.
     *
     * Thread-safe: inference calls are serialised internally.
     *
     * @throws std::invalid_argument  for empty or invalid-UTF-8 input.
     * @throws std::runtime_error     when inference fails or the output shape is unexpected.
     */
    [[nodiscard]] std::vector<float> embedText(const std::string& text) const;

    /**
     * Reads a UTF-8 text file and returns its embedding.
     * Satisfies REQ-2.2.1.2: file content is vectorised before being stored in the DB.
     *
     * @throws std::runtime_error     if the file cannot be opened or read.
     * @throws std::invalid_argument  if the file is empty or not valid UTF-8.
     */
    [[nodiscard]] std::vector<float> embedFile(const std::filesystem::path& filePath) const;

    /**
     * Embeds a batch of texts sequentially using one loaded model instance.
     * Prefer this over repeated embedText() calls when indexing many files.
     */
    [[nodiscard]] std::vector<std::vector<float>> embedTexts(
        const std::vector<std::string>& texts) const;

    /** Returns the model output dimension (384 for all-MiniLM-L6-v2). */
    [[nodiscard]] int getDimension() const noexcept;

private:
    struct TokenizedInput {
        std::vector<std::int64_t> inputIds;
        std::vector<std::int64_t> attentionMask;
        std::vector<std::int64_t> tokenTypeIds;
    };

    void loadVocabulary();
    [[nodiscard]] TokenizedInput tokenize(std::string_view text) const;
    [[nodiscard]] static bool        isValidUtf8(std::string_view text) noexcept;
    [[nodiscard]] static std::string readUtf8File(const std::filesystem::path& filePath);
    [[nodiscard]] static std::string toLowerAscii(std::string_view text);
    [[nodiscard]] static std::vector<float> l2Normalize(std::vector<float> values);

    Options                                        options_;
    std::unordered_map<std::string, std::int64_t>  vocabulary_;
    std::unique_ptr<Ort::Env>                      environment_;
    std::unique_ptr<Ort::SessionOptions>           sessionOptions_;
    std::unique_ptr<Ort::Session>                  session_;
    std::vector<std::string>                       inputNames_;
    std::vector<std::string>                       outputNames_;
    mutable std::mutex                             inferenceMutex_;
};

} // namespace nlp

#endif // FILEMANAGER_EMBEDDER_HPP
