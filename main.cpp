#include <iostream>
#include <chrono>

#include "replace.hpp"


namespace {

// Text file with utf-8 code points
constexpr std::string_view kDataFileName = "Bijankhan_Corpus_large.txt";
// Dictionary file path. The file should contain rows of tab separated chars.
// The test folder has some examples.
constexpr std::string_view kDictFileName = "dict.tsv";

} // namespace


int main(int argc, char* argv[])
{
    auto start_time = std::chrono::high_resolution_clock::now();
    // Load dictionary file from disc. It should contain rows of tab separated chars
    auto [search_table, search_ascii] =
        utf8mr::CreateDictionary(static_cast<std::string>(kDictFileName));
    // Get text file handler
    auto input_file = utf8mr::TouchFile(static_cast<std::string>(kDataFileName));
    // Load file by chunks and pass processing
    auto result = utf8mr::ProcessByWorkers(std::move(input_file),
                                           search_table,
                                           search_ascii);
    // Wait for all tasks to reach
    for (auto&& worker : result) {
        worker.wait();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Elapsed time: " << duration << "ms" << std::endl;

    return 0;
}

