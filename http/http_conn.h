#ifndef WORKSPACE_HTTP_CONN_H
#define WORKSPACE_HTTP_CONN_H

#include <cstdio>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

class http_conn {
public:
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUEST_LINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUEST_LINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn() {}
    ~http_conn(){}

public:
    void process(); // 处理客户端请求
    void init(int sock_fd,const sockaddr_in& address); // 初始化新接受的连接
    void close_conn(); // 关闭连接
    bool read(); // 非阻塞读
    bool write(); // 非阻塞写


public:
    static int m_epoll_fd;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
    static int m_user_count;  // 统计用户的数量

public:
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 2048; // 写缓冲区大小

private:
    int m_sock_fd; // 该HTTP连接的socket和对方的socket地址
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE]; // 读缓存区
    int m_read_idx; // 标识读缓冲区中已经读入的最后一个字节的下一个位置

    int m_checked_index; // 当前正在分析的字符在缓冲区中的位置
    int m_start_line; // 当前正在解析的行的位置

    CHECK_STATE m_check_state; // 主状态机当前的状态
    METHOD m_method; // 请求方法

    bool m_linger;  // http请求是否要求保存连接
    char* m_url; // 用户请求的文件名
    char* m_version; // http协议版本 这里目前仅支持 http1.1
    char* m_host; // 主机名

    void init(); // 初始化连接其余的信息
    HTTP_CODE process_read();    // 解析HTTP请求
    HTTP_CODE parse_request_line(char* text); // 解析请求行
    HTTP_CODE parse_request_headers(char* text); // 解析请求头
    HTTP_CODE parse_request_content(char* text); // 解析请求体

    LINE_STATUS parse_line(); // 解析行
    char* get_line(){return m_read_buf + m_start_line;} // 获取一行数据
    HTTP_CODE do_request(); // 具体请求处理

};

#endif //WORKSPACE_HTTP_CONN_H
