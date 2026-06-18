#include "core/database.h"
#include "core/indexer.h"
#include "embedder.hpp"
#include "search/smart_search.h"

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
struct CliOptions {
    std::string dbPath = "local_database.db";
    std::string modelPath = "models/all-MiniLM-L6-v2.onnx";
    std::string vocabPath = "models/vocab.txt";
    std::string indexDirectory;
    std::string prompt;
    std::size_t topK = 5;
    std::size_t contentLimit = 500;
    bool helpRequested = false;
};

void printUsage(std::string_view exeName) {
    std::cout
        << "Usage: " << exeName << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --db <path>       SQLite database path (default: local_database.db)\n"
        << "  --model <path>    ONNX model path (default: models/all-MiniLM-L6-v2.onnx)\n"
        << "  --vocab <path>    BERT vocab path (default: models/vocab.txt)\n"
        << "  --index <dir>     Index this directory and store embeddings before searching\n"
        << "  --prompt <text>   Run one search and exit; otherwise an interactive prompt is used\n"
        << "  --top <n>         Number of results to show (default: 5)\n"
        << "  --content-limit <n>  Max small text files to content-embed while indexing (default: 500)\n"
        << "  --help            Show this help\n";
}

bool readValue(const std::vector<std::string>& args, int& i, std::string& value) {
    if (i + 1 >= static_cast<int>(args.size())) {
        return false;
    }
    value = args[static_cast<std::size_t>(++i)];
    return true;
}

bool parseArgs(const std::vector<std::string>& args, CliOptions& options) {
    for (int i = 1; i < static_cast<int>(args.size()); ++i) {
        const std::string& arg = args[static_cast<std::size_t>(i)];
        if (arg == "--help" || arg == "-h") {
            options.helpRequested = true;
            return true;
        }
        if (arg == "--db") {
            if (!readValue(args, i, options.dbPath)) return false;
        } else if (arg == "--model") {
            if (!readValue(args, i, options.modelPath)) return false;
        } else if (arg == "--vocab") {
            if (!readValue(args, i, options.vocabPath)) return false;
        } else if (arg == "--index") {
            if (!readValue(args, i, options.indexDirectory)) return false;
        } else if (arg == "--prompt") {
            if (!readValue(args, i, options.prompt)) return false;
        } else if (arg == "--top") {
            std::string raw;
            if (!readValue(args, i, raw)) return false;
            options.topK = static_cast<std::size_t>(std::stoul(raw));
        } else if (arg == "--content-limit") {
            std::string raw;
            if (!readValue(args, i, raw)) return false;
            options.contentLimit = static_cast<std::size_t>(std::stoul(raw));
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    return options.topK > 0;
}

#ifdef _WIN32
std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        value.data(),
                        static_cast<int>(value.size()),
                        &utf8[0], size, nullptr, nullptr);
    return utf8;
}

std::vector<std::string> getCommandLineArgs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    if (!argv) {
        return args;
    }

    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(wideToUtf8(argv[i]));
    }
    LocalFree(argv);
    return args;
}
#else
std::vector<std::string> getCommandLineArgs(int argc, char* argv[]) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return args;
}
#endif

void printResults(const std::vector<SmartSearchResult>& results) {
    if (results.empty()) {
        std::cout << "No embedded files matched this prompt. If the database is empty, run again with --index <dir>.\n";
        return;
    }

    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        std::cout << i + 1 << ". score=" << result.score
                  << " name=\"" << result.record.file_name << "\""
                  << " path=\"" << result.record.file_path << "\"";
        if (!result.record.last_modified.empty()) {
            std::cout << " modified=\"" << result.record.last_modified << "\"";
        }
        std::cout << "\n";
    }
}

void silenceCerrDuringSuccessfulShutdown() {
#ifdef _WIN32
    static auto* sink = new std::ostringstream();
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    const int nullFd = _open("NUL", _O_WRONLY);
    if (nullFd != -1) {
        _dup2(nullFd, _fileno(stdout));
        _dup2(nullFd, _fileno(stderr));
        _close(nullFd);
    }
    static HANDLE nullHandle = CreateFileA("NUL",
                                           GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           nullptr,
                                           OPEN_EXISTING,
                                           FILE_ATTRIBUTE_NORMAL,
                                           nullptr);
    if (nullHandle != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, nullHandle);
        SetStdHandle(STD_ERROR_HANDLE, nullHandle);
    }
    std::cout.rdbuf(sink->rdbuf());
    std::cerr.rdbuf(sink->rdbuf());
#endif
}
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    const std::vector<std::string> args = getCommandLineArgs();
    (void)argc;
    (void)argv;
#else
    const std::vector<std::string> args = getCommandLineArgs(argc, argv);
#endif

    CliOptions options;
    if (!parseArgs(args, options)) {
        printUsage(args.empty() ? "smart_search_cli" : args.front());
        return 1;
    }
    if (options.helpRequested) {
        printUsage(args.empty() ? "smart_search_cli" : args.front());
        return 0;
    }

    try {
        nlp::Embedder::Options embedderOptions;
        embedderOptions.modelPath = options.modelPath;
        embedderOptions.vocabPath = options.vocabPath;

        nlp::Embedder embedder(embedderOptions);
        DatabaseManager db(options.dbPath);

        if (!options.indexDirectory.empty()) {
            Indexer indexer(db, "C:", false);
            indexer.scanDirectoryWithEmbeddings(options.indexDirectory, embedder, options.contentLimit);
        }

        SmartSearch searcher(db);
        if (!options.prompt.empty()) {
            printResults(searcher.search(options.prompt, embedder, options.topK));
            silenceCerrDuringSuccessfulShutdown();
            return 0;
        }

        std::cout << "Smart Search ready. Enter an empty prompt to exit.\n";
        std::string prompt;
        while (true) {
            std::cout << "smart> ";
            if (!std::getline(std::cin, prompt) || prompt.empty()) {
                break;
            }
            printResults(searcher.search(prompt, embedder, options.topK));
        }
        silenceCerrDuringSuccessfulShutdown();
    } catch (const std::exception& e) {
        std::cerr << "Smart Search failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
