#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _capacity(capacity), _bytes_read(0), _bytes_write(0), _buffer(), _end(false), _error(false) {}

size_t ByteStream::write(const string &data) {
    const size_t remains = _capacity - (_bytes_write - _bytes_read);
    // `bytes` is the bytes can be written into the stream
    const size_t bytes = remains >= data.size() ? data.size() : remains;

    for (const char &ch : data.substr(0, bytes)) {
        _buffer.push_back(ch);
    }

    _bytes_write += bytes;

    return bytes;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    const size_t peek_size = std::min(len, _buffer.size());
    string peek = string(_buffer.begin(), _buffer.begin() + peek_size);
    return peek;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    const size_t pop_size = std::min(len, _buffer.size());
    _bytes_read += pop_size;

    for (size_t i = 0; i < pop_size; i++) {
        _buffer.pop_front();
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string data = peek_output(len);
    pop_output(len);

    return data;
}

void ByteStream::end_input() { _end = true; }

bool ByteStream::input_ended() const { return _end; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _end && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_write; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - (_bytes_write - _bytes_read); }
