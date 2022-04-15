#include "http_conn.h"

int http_conn::m_epoll_fd = -1;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_user_count = 0;  // 所有的客户数

// 设置文件描述符非阻塞
void set_nonblocking(int fd) {
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
}

// 向epoll中添加需要监听的文件描述符
void add_fd(int epoll_fd,int fd,bool one_shot){
    epoll_event event{};
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    //event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    if(one_shot) {
        // 防止同一个通信被不同的线程处理
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event);
    // 设置文件描述符非阻塞
    set_nonblocking(fd);
}
// 从epoll中移除监听的文件描述符
void remove_fd(int epoll_fd,int fd){
    epoll_ctl(epoll_fd,EPOLL_CTL_DEL,fd,nullptr);
    close(fd);
}
// 修改文件描述符，重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void mod_fd(int epoll_fd,int fd,int ev) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd,EPOLL_CTL_MOD,fd,&event);
}
// 初始化连接,外部调用初始化套接字地址
void http_conn::init(int sock_fd, const sockaddr_in &address){
    m_sock_fd = sock_fd;
    m_address = address;
    // 端口复用
    int reuse = 1;
    setsockopt(m_sock_fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof (reuse));
    // 添加到epoll对象中
    add_fd(m_epoll_fd,sock_fd,true);
    m_user_count++;  // 总用户数+1

    init();
}

// 初始化
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUEST_LINE; //  初始状态为解析请求首行

    m_start_line = 0;
    m_checked_index = 0;
    m_read_idx = 0;
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_linger = false;

    bzero(m_read_buf,READ_BUFFER_SIZE);

}

// 关闭连接
void http_conn::close_conn() {
    if(m_sock_fd != -1){
        remove_fd(m_epoll_fd,m_sock_fd);
        m_sock_fd = -1;
        m_user_count--; // 关闭一个连接，将客户总数量-1
    }
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read() {
    if(m_read_idx >= READ_BUFFER_SIZE) return false;

    // 读取到的字节
    int bytes_read = 0;
   while(true) {
       // 从m_read_buf + m_read_idx索引处开始保存数据，大小是READ_BUFFER_SIZE - m_read_idx
       bytes_read = recv(m_sock_fd,m_read_buf + m_read_idx,READ_BUFFER_SIZE - m_read_idx,0);
       if(bytes_read == -1) {
           if(errno == EAGAIN || errno == EWOULDBLOCK) {
               // 没有数据
               break;
           }
           return false;
       } else if(bytes_read == 0) {
           // 对方关闭连接
           return false;
       }
       m_read_idx += bytes_read;  // 更新下次读取的位置
   }
    printf("读取到数据: %s\n",m_read_buf);
    return true;
}

// 主状态机
http_conn::HTTP_CODE http_conn::process_read() { // 解析HTTP请求
    // 初始状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = nullptr;

    while((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)
        || ((line_status = parse_line()) == LINE_OK)) {
        // 解析到请求体（完整的）  或者或者解析到一行完整的数据

        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;  // 更新解析行的位置
        printf("got one http line: %s\n",text);

        switch (m_check_state) {
            case CHECK_STATE_REQUEST_LINE:{
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_request_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_request_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN; // 不完整
                break;
            }
            default:{
            return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){ // 解析请求行
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text," \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if(!m_url) {
        return BAD_REQUEST;
    }
    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';   // 置位空字符，字符串结束符
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    m_version = strpbrk( m_url, " \t" );
    if (!m_version) {
        return BAD_REQUEST;
    }
    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if (strcasecmp( m_version, "HTTP/1.1") != 0 ) { // 这里只判断了 http 1.1
        return BAD_REQUEST;
    }
    // http://192.168.0.1:10000/index.html
    if (strncasecmp(m_url, "http://", 7) == 0 ) { // 检查url是否合法
        m_url += 7;  	// 192.168.1.11:10000/index.html
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );  // /index.html
    }
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER; // 主状态机检查状态变成检查请求头
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_request_headers(char* text) { // 解析请求头

}

http_conn::HTTP_CODE http_conn::parse_request_content(char* text) { // 解析请求体

}

// 解析一行 判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_index < m_read_idx; ++m_checked_index) {
        // 当前要解析的字符
        temp = m_read_buf[m_checked_index];
        if(temp == 'r') {
            // 如果\r 刚好是buffer 中的最后一个字符 则说明数据可能不完整 返回 LINE_OPEN
            if((m_checked_index + 1) == m_read_idx) {
                return LINE_OPEN;
            } else if(m_read_buf[m_checked_index + 1] == '\n') {
                // 读取到完整的一行 将 \r\n 替换为\0结束
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD; // 否则可能是语法错误
        } else if(temp == '\n') {
            if(m_checked_index > 1 && m_read_buf[m_checked_index - 1] == '\r') {
                // 如果当前字符为 \n 则也说明可能读到完整的行
                m_read_buf[m_checked_index - 1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; // 没有遇到 \r 说明数据不完整
}

http_conn::HTTP_CODE http_conn::do_request() {

}


bool http_conn::write() {
    printf("一次性写完数据\n");
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求

    HTTP_CODE process_read;

    printf("parse request,create response\n");

    // 生成响应
}
