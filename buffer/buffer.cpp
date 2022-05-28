#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer(initBuffSize), readIndex(0), writeIndex(0) {}

size_t Buffer::ReadableBytes() const {  // 可读字节数
    return writeIndex - readIndex;
}
size_t Buffer::WritableBytes() const {      // 可写字节数
    return buffer.size() - writeIndex;
}


const char* Buffer::Peek() const {      // 获取读位置指针
    return BeginPtr_() + readIndex;
}


void Buffer::RetrieveAll() {        // 重置 buffer
    bzero(&buffer[0], buffer.size());
    readIndex = 0;
    writeIndex = 0;
}

std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}


char* Buffer::BeginWrite() {            // 获得写位置的指针
    return BeginPtr_() + writeIndex;
}

void Buffer::HasWritten(size_t len) {       // 移动可写下标
    writeIndex += len;
}

void Buffer::Append(const std::string& str) {    //   添加数据到buffer中
    Append(str.data(), str.length());
}


void Buffer::Append(const char* str, size_t len) {      // 添加数据到buffer中
    assert(str);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

char* Buffer::BeginPtr_() {
    return &*buffer.begin();
}

const char* Buffer::BeginPtr_() const {
    return &*buffer.begin();
}
