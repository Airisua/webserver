#ifndef WORKSPACE_BUFFER_H
#define WORKSPACE_BUFFER_H

#include <cstring>   //perror
#include <iostream>
#include <unistd.h>  // write
#include <sys/uio.h> //readv
#include <vector> //readv
#include <atomic>
#include <cassert>

class Buffer{
public:
    explicit Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    size_t WritableBytes() const;
    size_t ReadableBytes() const ;

    const char* Peek() const;
    void HasWritten(size_t len);


    void RetrieveAll() ;
    std::string RetrieveAllToStr();
    char* BeginWrite();

    void Append(const std::string& str);
    void Append(const char* str, size_t len);

private:
    char* BeginPtr_();
    const char* BeginPtr_() const;

    std::vector<char> buffer;
    std::atomic<std::size_t> readIndex;
    std::atomic<std::size_t> writeIndex;

};


#endif //WORKSPACE_BUFFER_H
