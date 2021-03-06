#include <cstring>

#include "replace.hpp"


namespace utf8mr {

// Returns the length of one's block at leftmost of char.
// For example:
//      Input: 11010100, Returns: 2
//      Input: 11110011, Returns: 4
uint8_t LeftmostBlockSize(const uint8_t chr)
{
    uint8_t size{2};

    // Left shift 8 bits binary to reach the first zero
    while (size <= 4) {
        uint8_t shifted_char = chr << size;
        uint8_t leftmost_bit = shifted_char & 0x80;
        if(!leftmost_bit)
            break;
        ++size;
    }

    return size;
}

// Load dictionary file from disc and store its tab separated rows in a hashtable.
// Also, it will be checked that if there exist any ASCII char in seek list.
std::pair<StringDictionary, bool> CreateDictionary(const std::string& file_path)
{
    std::ifstream dictionary_file{file_path};
    bool search_ascii{false};
    
    StringDictionary search_table{};
    std::string line{};
    // Read each line of the file and put its key:value in hashtable
    while (std::getline(dictionary_file, line)) {
        std::istringstream current_line(line);
        std::vector<std::string> pair((std::istream_iterator<std::string>(current_line)),
                                            std::istream_iterator<std::string>());
        // If replace chars do not exist, empty string will be replaced
        if (pair.size() == 1) {
            pair.emplace_back("");
        }
        search_table.insert(std::make_pair(pair[0], pair[1]));
        // Check for ASCII
        if (pair[0].size() == 1)
            search_ascii = true;
    }

    return std::make_pair(search_table, search_ascii);
}


std::ifstream TouchFile(const std::string& file_path)
{
    std::ifstream input_file{file_path};
    // Other preprocessing and validation ..
    return input_file;
}


uint64_t GetFileLength(std::ifstream& file)
{
    uint64_t file_len{0};

    file.seekg(0, file.end);
    file_len = static_cast<uint64_t>(file.tellg());
    file.seekg(0, file.beg);

    return file_len;
}


// Returns Vector of asynchronous tasks.
// Read text file in chunks and pass each chunk to one task/worker for processing.
// Chunk length will be deduced from file size and number of CPU`s hyper threads.
std::vector<std::future<std::string>> ProcessByWorkers(std::ifstream&& input_file,
                                                       const StringDictionary& search_table,
                                                       bool search_ascii)
{
    std::vector<std::future<std::string>> workers;
    // We choose size from this formula to reach as many tasks as the number of CPU`s hyper threads
    const uint64_t kChunkSize = GetFileLength(input_file) / 
                                std::thread::hardware_concurrency();
    
    while (!input_file.eof() && !input_file.fail()) {
        std::string chunk(kChunkSize,'\0');
        input_file.read(chunk.data(), kChunkSize);
        chunk.resize(input_file.gcount());

        workers.emplace_back(std::async([&](std::string&& data){
                return Replace(std::move(data), // move the ownership
                               data.size(),
                               search_table,
                               search_ascii);
            }, std::move(chunk)));
    }

    return workers;
}


// Returns start points of unicode code points in vector of 16 char
std::unique_ptr<uint8_t[]> GetStartPoint(__m128i& chunk)
{
    // Because compare only works with signed chars
    auto chunk_signed = _mm_add_epi8(chunk, _mm_set1_epi8(0x80));
    // Create a vector which contain number of succeeding chars of code points at
    // the position of first char. For example:
    //      src:            h e l l o سلام
    //      start_points_p: 0 0 0 0 0 1 0 1 0 1 0 1 0
    // More info at: https://woboq.com/blog/utf-8-processing-using-simd.html
    auto state = _mm_set1_epi8(0x0 | 0x80);
    auto w_2_bytes = _mm_cmplt_epi8( _mm_set1_epi8(0xc2 - 1 - 0x80), chunk_signed); // Two bytes code points
    state = _mm_blendv_epi8(state , _mm_set1_epi8(0x2 | 0xc2),  w_2_bytes);
    auto w_3_bytes = _mm_cmplt_epi8( _mm_set1_epi8(0xe0 - 1 - 0x80), chunk_signed); // Three bytes code points
    state = _mm_blendv_epi8(state , _mm_set1_epi8(0x3 | 0xe0),  w_3_bytes);
    auto w_4_bytes = _mm_cmplt_epi8(_mm_set1_epi8(0xf0 - 1 - 0x80), chunk_signed); // Four bytes code points
    state = _mm_blendv_epi8(state , _mm_set1_epi8(0x4 | 0xf0),  w_4_bytes);
    auto start_points_p =  _mm_and_si128(state, _mm_set1_epi8(0x7));

    auto start_points = std::make_unique<uint8_t[]>(16);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(start_points.get()), start_points_p);

    return start_points;
}


std::string ReallocateIfNotEnough(std::string& str,
                                  const uint64_t current_pos,
                                  const uint64_t len_required)
{
    if (str.size() - current_pos < len_required) {
        str.resize(current_pos + len_required, '\0');
    }

    return str;
}


std::string ReplaceCharByChar(std::string& src,
                              const StringDictionary& search_table)
{
    uint8_t src_len = src.size();
    std::string dest(src_len * 4, '\0');
    auto c_src = src.c_str();
    uint8_t current_position{0};
    uint8_t src_cursor_position{0};
    uint8_t dst_cursor_position{0};
    while (current_position < src_len) {
        if (!(src[current_position] & 0x80)) { // ASCII 0x0-------
            memcpy(dest.data() + dst_cursor_position,
                   c_src + src_cursor_position,
                   1);
            ++src_cursor_position;
            ++dst_cursor_position;
            ++current_position;
            continue;
        }
        // Get type of code point
        uint8_t codepoint_len = LeftmostBlockSize(src[current_position]);
        std::string seek(c_src + current_position, codepoint_len);
        // Search for code point in hashtable
        StringDictionary::const_iterator it = search_table.find(seek);
        // If Search has any result
        if (it != search_table.end()) {
            std::string result{it->second};
            memcpy(dest.data() + dst_cursor_position,
                   result.data(),
                   result.size());

            dst_cursor_position += result.size();
        } else {
            // Just copy from source to destination string
            memcpy(dest.data() + dst_cursor_position,
                   c_src + src_cursor_position,
                   codepoint_len);

            dst_cursor_position += codepoint_len;
        }

        src_cursor_position += codepoint_len;
        current_position += codepoint_len;
    }
    // Strip trailing zero chars from destination
    dest.resize(dst_cursor_position);

    return dest;
}


// Returns s string which seek code points replced in it.
// We use SIMD 128 bit vector to improve performance.
std::string Replace(const std::string&& src,
                    uint64_t src_len,
                    const StringDictionary& search_table,
                    bool search_ascii)
{
    std::string dest(src_len * 2, '\0'); // start with double size of input, it expands if necessary
    auto c_src = src.c_str();
    
    uint64_t current_position{0};
    uint64_t src_cursor_position{0}; // The last read/processed char in source
    uint64_t dst_cursor_position{0}; // The latest written byte position
    uint64_t unwritten_bytes{0}; // Number of skipped char that should be just copied
    
    while (src_len - current_position >= 16) { // Work with 128 bits (16x8)
        auto chunk = _mm_loadu_si128(static_cast<const __m128i*>(static_cast<const void*>(c_src + current_position)));
        if (!_mm_movemask_epi8(chunk) && !search_ascii) { // Skip ASCIIs
            current_position += 16;
            unwritten_bytes += 16;
            continue;
        }

        auto start_points = GetStartPoint(chunk);
        uint8_t position_in_chunk{0};
        while (position_in_chunk < 16) {
            if (uint8_t codepoint_len = start_points[position_in_chunk]) {
                std::string seek(c_src + current_position + position_in_chunk, codepoint_len);
                // Search for code point in hashtable
                StringDictionary::const_iterator it = search_table.find(seek);
                // Replace chars found
                if (it != search_table.end()) {
                    const std::string result{it->second};
                    // First copy any skipped chars
                    if (unwritten_bytes) {
                        dest = ReallocateIfNotEnough(dest,
                                                     dst_cursor_position,
                                                     unwritten_bytes + src_len - src_cursor_position);
                        // Copy skipped to destination
                        memcpy(dest.data() + dst_cursor_position,
                               c_src + src_cursor_position,
                               unwritten_bytes);

                        src_cursor_position += unwritten_bytes;
                        dst_cursor_position += unwritten_bytes;
                    }
                    // Copy result of search in hashtable to destination string
                    memcpy(dest.data() + dst_cursor_position,
                           result.data(),
                           result.size());

                    unwritten_bytes = 0;
                    src_cursor_position += codepoint_len;
                    dst_cursor_position += result.size();
                    position_in_chunk += codepoint_len;
                    continue;
                }
                // If search in hashtable has not any result, code point will be copied in next loop 
                unwritten_bytes += codepoint_len;
                position_in_chunk += codepoint_len;
                continue;
            }
            // If its a ASCII chars it will be copied in next loop
            ++unwritten_bytes;
            ++position_in_chunk;
        }
        current_position += position_in_chunk;
    }
    // Copy any skipped chars remained after last loop
    if (unwritten_bytes) {
       memcpy(dest.data() + dst_cursor_position,
              c_src + src_cursor_position,
              unwritten_bytes);

        src_cursor_position += unwritten_bytes;
        dst_cursor_position += unwritten_bytes;
       unwritten_bytes = 0;
    }
    // Strip trailing zero chars from destination
    dest.resize(dst_cursor_position);

    if (current_position < src_len) {
        auto tail_part = src.substr(current_position, src_len - current_position);
        auto last_processed_part = ReplaceCharByChar(tail_part,
                                                     search_table);
        dest += last_processed_part;
    }
 
    return dest;
}

} // namespace

