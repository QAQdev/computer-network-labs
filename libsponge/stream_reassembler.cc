#include "stream_reassembler.hh"

#include <cassert>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _unassemble_strs()
    , _next_assembled_idx(0)
    , _unassembled_bytes_num(0)
    , _eof_idx(-1)
    , _output(capacity)
    , _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    /**
     * 传入的 substring 可能有以下几种情况
     * NOTE: 需要考虑到, _output 暂时装入不下的情况
     *
     * 1. index <=  _next_assembled_idx && index + data.size() > _next_assembled_idx
     *    这种可以截断前后部重复 data 后直接装配，无需任何额外处理.
     *    如果装不下，则插入 _unassemble_strs 中并返回
     * 2. index > _next_assembled_idx
     *    这种是需要认真考虑的，因为这种情况可能有一些字串重合，导致大量的内存占用以及无用的轮询处理
     *    首先获取 index 的上一个已经插入的 idx，下称 up_idx
     *      a. 若 up_idx + up_data.size() <= index，则说明前半部分字串没有重合
     *      b. 若 up_idx + up_data.size() > index，说明前半部分重合，将重合部分丢弃，并更新 index
     *    之后获取 up_idx 的下一个已经插入的 idx，下称 down_idx
     *      a. 若 index + data.size() <= down_idx，则说明后半部分字串没有重合
     *      b. 若 index + data.size() > down_idx，说明后半部分重合
     *          NOTE: 需要注意的是，后半部分重合又有两种情况，一种是部分重合，另一种是与 down_data 完全重合。
     *    如果最后发现整个字符串经过丢弃处理后完全清空，则直接返回
     *    否则，将其插入至 _unassemble_strs 中，确保该结构中的每个字符串都不重合
     * 3. 其余情况直接丢弃
     *
     *    NOTE: 对于性能优化的考虑，可以考虑在丢弃重合部分后，合并至上一个或者下一个
     *          不过简单思考了一下，貌似合并操作的开销比插入一个新的pair至map中要大
     *          因为 substring 的长度可能是非常长的，但 map 的深度增长是比较慢的
     * 在每次成功执行装配操作后，遍历 _unassemble_strs 以继续进行装配
     * 直到遇到了一个无法装配的 substrs，或者 _output 已满
     *
     * NOTE: 按照上述的操作，最终我们可以确保 _unassemble_strs 中没有子串是相互重叠的
     *
     * 所以最终的实现情况是这样的：
     *  1. 获取当前 index 在 _unassemble_strs 中的上界 pair
     *      a. 如果存在上界 pair，则判断是否存在重复区域。如果存在部分重复则截断，存在全部重复则直接丢弃。
     *      b. 如果不存在上界 pair, 则判断当前数据是否已经和已经被 assemble 的字符串重复。
     *         如果存在部分重复则截断，存在全部重复则直接丢弃。
     *  2. 获取当前 index 在 _unassemble_strs 中的下界 pair。 如果存在下界 pair
     *      a. 如果下界 pair 没有被当前数据完全包含，则判断是否存在重复区域。如果存在则截断。
     *      b. 如果下界 pair 被当前数据包含，则将下界 pair 从 _unassemble_strs 中丢弃，之后重新取出一个新的下界 pair,
     * 重复第二步 这时候我们可以获得与 _unassemble_strs 中没有任何重复的字符串
     *
     * NOTE: 本人的整体操作是尽可能的降低内存消耗，用时间来换取空间，毕竟私以为数据比较珍贵，不能随意丢弃。
     */

    // 获取 map 中第一个大于 index 的迭代器指针
    auto pos_iter = _unassemble_strs.upper_bound(index);
    // 尝试获取一个小于等于 index 的迭代器指针
    if (pos_iter != _unassemble_strs.begin())
        pos_iter--;
    /**
     *  此时迭代器有三种情况，
     *  1. 一种是为 end_iter，表示不存在任何数据
     *  2. 一种是为非 end_iter，
     *      a. 一种表示确实存在一个小于等于 idx 的迭代器指针
     *      b. 一种表示当前指向的是大于 idx 的迭代器指针
     */

    // 获取当前子串的新起始位置
    size_t new_idx = index;
    // 如果前面确实有子串
    if (pos_iter != _unassemble_strs.end() && pos_iter->first <= index) {
        const size_t up_idx = pos_iter->first;

        // 如果当前子串前面出现了重叠
        if (index < up_idx + pos_iter->second.size())
            new_idx = up_idx + pos_iter->second.size();
    }
    // 如果前面没有子串，则和当前读取到的pos进行比较
    else if (index < _next_assembled_idx)
        new_idx = _next_assembled_idx;

    // 子串新起始位置对应到的 data 索引
    const size_t data_start_pos = new_idx - index;
    // 当前子串将保存的 data 的长度
    /**
     *  NOTE: 注意，若上一个子串将当前子串完全包含，则 data_size 不为正数
     *  NOTE: 这里由于 unsigned 的结果向 signed 的变量写入，因此可能存在上溢的可能
     *        但即便上溢，最多只会造成当前传入子串丢弃，不会产生任何安全隐患
     *  PS: 而且哪个数据包会这么大，大到超过 signed long 的范围
     */
    ssize_t data_size = data.size() - data_start_pos;

    // 获取当前子串的下个子串时，需要考虑到 new_idx 可能会和 down_idx 重合
    pos_iter = _unassemble_strs.lower_bound(new_idx);
    // 下面注释的这两条语句与上面的 lower_bound 等价
    // if (pos_iter != _unassemble_strs.end() && pos_iter->first <= new_idx)
    //     ++pos_iter;

    // 如果和下面的子串冲突，则截断长度
    while (pos_iter != _unassemble_strs.end() && new_idx <= pos_iter->first) {
        const size_t data_end_pos = new_idx + data_size;
        // 如果存在重叠区域
        if (pos_iter->first < data_end_pos) {
            // 如果是部分重叠
            if (data_end_pos < pos_iter->first + pos_iter->second.size()) {
                data_size = pos_iter->first - new_idx;
                break;
            }
            // 如果是全部重叠
            else {
                _unassembled_bytes_num -= pos_iter->second.size();
                pos_iter = _unassemble_strs.erase(pos_iter);
                continue;
            }
        }
        // 如果不重叠则直接 break
        else
            break;
    }
    // 检测是否存在数据超出了容量。注意这里的容量并不是指可保存的字节数量，而是指可保存的窗口大小
    //! NOTE: 注意这里我们仍然接收了 index 小于 first_unacceptable_idx  但
    //        index + data.size >= first_unacceptable_idx 的那部分数据
    //        这是因为处于安全考虑，最好减少算术运算操作以避免上下溢出
    //        同时多余的那部分数据最多也只会多占用 1kb 左右，属于可承受范围之内
    size_t first_unacceptable_idx = _next_assembled_idx + _capacity - _output.buffer_size();
    if (first_unacceptable_idx <= new_idx)
        return;

    // 判断是否还有数据是独立的， 顺便检测当前子串是否被上一个子串完全包含
    if (data_size > 0) {
        const string new_data = data.substr(data_start_pos, data_size);
        // 如果新字串可以直接写入
        if (new_idx == _next_assembled_idx) {
            const size_t write_byte = _output.write(new_data);
            _next_assembled_idx += write_byte;
            // 如果没写全，则将其保存起来
            if (write_byte < new_data.size()) {
                // _output 写不下了，插入进 _unassemble_strs 中
                const string data_to_store = new_data.substr(write_byte, new_data.size() - write_byte);
                _unassembled_bytes_num += data_to_store.size();
                _unassemble_strs.insert(make_pair(_next_assembled_idx, std::move(data_to_store)));
            }
        } else {
            const string data_to_store = new_data.substr(0, new_data.size());
            _unassembled_bytes_num += data_to_store.size();
            _unassemble_strs.insert(make_pair(new_idx, std::move(data_to_store)));
        }
    }

    // 一定要处理重叠字串的情况
    for (auto iter = _unassemble_strs.begin(); iter != _unassemble_strs.end(); /* nop */) {
        // 如果当前刚好是一个可被接收的信息
        assert(_next_assembled_idx <= iter->first);
        if (iter->first == _next_assembled_idx) {
            const size_t write_num = _output.write(iter->second);
            _next_assembled_idx += write_num;
            // 如果没写全，则说明写满了，保留剩余没写全的部分并退出
            if (write_num < iter->second.size()) {
                _unassembled_bytes_num += iter->second.size() - write_num;
                _unassemble_strs.insert(make_pair(_next_assembled_idx, std::move(iter->second.substr(write_num))));

                _unassembled_bytes_num -= iter->second.size();
                _unassemble_strs.erase(iter);
                break;
            }
            // 如果写全了，则删除原有迭代器，并进行更新
            _unassembled_bytes_num -= iter->second.size();
            iter = _unassemble_strs.erase(iter);
        }
        // 否则直接离开
        else
            break;
    }
    if (eof)
        _eof_idx = index + data.size();
    if (_eof_idx <= _next_assembled_idx)
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes_num; }

bool StreamReassembler::empty() const { return _unassembled_bytes_num == 0; }
