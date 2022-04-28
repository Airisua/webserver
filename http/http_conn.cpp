#include "http_conn.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int http_conn::m_epoll_fd = -1;  // 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_user_count = 0;  // 所有的客户数

// 网站跟目录
//const char* doc_root = "/home/wlic/workspace/webserver/resource";
//  const char* doc_root = "/wyj/workspace/webserver/resource";
  const char* doc_root = "/home/wangyujin/wyj/webserver/resource";

  // 存放用户名密码
   std::map<std::string,std::string> users;

   locker m_lock;
   void http_conn::init_mysql_result(connection_pool *conn_pool) {
       // 先从连接池中获取一个连接
        MYSQL* mysql = nullptr;
        connection_RAII m_conn(&mysql,conn_pool);

       // 在user表中检索username，passwd数据，浏览器端输入
       if(mysql_query(mysql,"SELECT username,passwd FROM user")) {
           std::cout << "SELECT error: " << mysql_errno(mysql) << std::endl;
       }

       //从表中检索完整的结果集
       auto res = mysql_store_result(mysql);
       //返回结果集中的列数
       auto num_fields = mysql_num_fields(res);
       //返回所有字段结构的数组
       auto fields = mysql_fetch_field(res);
       //从结果集中获取下一行，将对应的用户名和密码，存入map中
       //auto row = mysql_fetch_row(res);
       while(auto row = mysql_fetch_row(res)) {
            std::string tmp1(row[0]);
            std::string tmp2(row[1]);
            users[tmp1] = tmp2;
       }
   }

// 设置文件描述符非阻塞
void set_nonblocking(int fd) {
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
}

// 向epoll中添加需要监听的文件描述符
// LT模式
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
    m_linger = false;  // 默认不保持链接  Connection : keep-alive保持连接
    m_method = GET;   // 默认请求方式为GET
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;

    m_start_line = 0;
    m_checked_index = 0;
    m_read_idx = 0;     // 重置写
    m_write_idx = 0;    // 重置读

    cgi = 0;
    m_content_length = 0;
    bytes_have_send = 0;
    bytes_to_send = 0;


    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
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
    ssize_t bytes_read = 0;
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

// m_start_line是行在buffer中的起始位置，将该位置后面的数据赋给text
// 此时从状态机已提前将一行的末尾字符\r\n变为\0\0，所以text可以直接取出完整的行进行解析
char* http_conn::get_line(){return m_read_buf + m_start_line;} // 获取一行数据


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

    // 取出数据，并通过与GET和POST比较，以确定请求方式
    char* method = text;
    if ( strcasecmp(method, "GET") == 0 ) { // 忽略大小写比较
        m_method = GET;
    } else if(strcasecmp(method,"POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }

    // m_url此时跳过了第一个空格或\t字符，但不知道之后是否还有
    // 将m_url向后偏移，通过查找，继续跳过空格和\t字符，指向请求资源的第一个字符
    m_url+=strspn(m_url," \t");

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
    // 类似http://192.168.0.1:10000/index.html
    if (strncasecmp(m_url, "http://", 7) == 0 ) { // 检查url是否合法
        m_url += 7;  	// 192.168.1.11:10000/index.html
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置。
        m_url = strchr( m_url, '/' );  // /index.html
    }
    // 增加 https
    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    // 一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
    if ( !m_url || m_url[0] != '/' ) {
        return BAD_REQUEST;
    }

    //当url为/时，显示主界面
    if(strlen(m_url) == 1) {
        strcat(m_url,"index.html");
    }

    m_check_state = CHECK_STATE_HEADER; // 请求处理完毕 主状态机检查状态变成检查请求头
    return NO_REQUEST;
}

// 解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_request_headers(char* text) { // 解析请求头
    // 遇到空行，表示头部字段解析完毕
    if( text[0] == '\0' ) {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if ( m_content_length != 0 ) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol(text);
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        printf( "oop! unknown header %s\n", text );
    }
    return NO_REQUEST;
}

// 这里没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_request_content(char* text){ // 解析请求体
    if ( m_read_idx >= ( m_content_length + m_checked_index ) )
    {
        text[ m_content_length ] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 解析一行 判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_index < m_read_idx; ++m_checked_index) {
        // 当前要解析的字符
        temp = m_read_buf[m_checked_index];
        if(temp == '\r') {
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

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    // "/home/wlic/workspace/webserver/resource"
    // 将初始化的m_real_file赋值为网站根目录
    strcpy( m_real_file, doc_root );
    int len = strlen(doc_root);

    const char *p = strrchr(m_url, '/');

    // 实现登录和注册校验
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        // 根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = new char[200];
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        delete[] m_url_real;

        // 将用户名和密码提取出来
        // user=123&password=123
        char name[200], password[200];
        int i;

        // 以&为分隔符，前面的为用户名
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        // 以&为分隔符，后面的是密码
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if(*(p + 1) == '3') {
            // 如果是注册，先检测数据库中是否有重名
            // 没有重名的，进行增加数据
            char* sql_insert = new char[200];
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 判断map中能否找到重复的用户名
            if(users.find(name) == users.end()) {
                // 向数据库中插入数据时，需要通过锁来同步数据
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(std::pair<std::string, std::string>(name, password));
                m_lock.unlock();

                // 效验成功，跳转登录页面
                if (!res) strcpy(m_url, "/login.html");
                    // 校验失败，跳转注册失败页面
                else strcpy(m_url, "/register_error.html");
            } else strcpy(m_url,"/login_error.html");
        }

        else if(*(p + 1) == '2') {
            // 如果是登录，直接判断
            // 若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/login_error.html");
        }

    }

    // 如果请求资源为/0，表示跳转注册界面
    if(*(p + 1) == '0') {

    }
    // 如果请求资源为/1，表示跳转登录界面
    else if(*(p + 1 ) == '1') {


    } else  // 如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
         strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    //  通过mmap将该文件映射到内存中
    m_file_address = ( char* )mmap( nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    // 避免文件描述符的浪费和占用
    close( fd );
    // 表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

// 取消内存映射
void http_conn::unmap() {
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );
        m_file_address = nullptr;
    }
}



// 写http响应
bool http_conn::write() {
    ssize_t temp = 0;
    // ssize_t bytes_have_send = 0;    // 已经发送的字节
    // ssize_t bytes_to_send = m_write_idx;// 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数

    if ( bytes_to_send == 0 ) {
        // 将要发送的字节为0，这一次响应结束。
        mod_fd( m_epoll_fd, m_sock_fd, EPOLLIN );
        init();
        return true;
    }
    while (true) {
        // 分散写  writev将不连续的缓冲区的数据一起写出去
        temp = writev(m_sock_fd,m_iv,m_iv_count);

        if ( temp <= -1 ) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if( errno == EAGAIN ) {
                mod_fd( m_epoll_fd, m_sock_fd, EPOLLOUT );
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
//        if(bytes_to_send <= bytes_have_send) {
//            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
//            unmap();
//            if(m_linger) {
//                init();
//                mod_fd( m_epoll_fd, m_sock_fd, EPOLLIN );
//                return true;
//            } else {
//                mod_fd( m_epoll_fd, m_sock_fd, EPOLLIN );
//                return false;
//            }
//        }
         // 判断响应头是否发送完毕
         if(bytes_have_send >= m_iv[0].iov_len) {
             // 头已经发送完毕
             m_iv[0].iov_len = 0;
             m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
             m_iv[1].iov_len = bytes_to_send;
         } else {
             // 没有发送完毕，更新下次写数据的位置
             m_iv[0].iov_base = m_write_buf + bytes_have_send;
             m_iv[0].iov_len = m_iv[0].iov_len - temp;
         }

         // 判断数据是否全部发送出去：
         if(bytes_to_send <= 0) {
             // 没有数据要发送了
             unmap();
             mod_fd(m_epoll_fd,m_sock_fd,EPOLLIN);

             if(m_linger) {
                 // 重新初始化http对象
                 init();
                 return true;
             } else {
                 return false;
             }
         }
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response( const char* format,... ) {  // 后面是个可变参数
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    //使用vsnprintf 将数据写入m_write_buf
    va_list arg_list;
    va_start( arg_list, format );   // 初始化 arg_list
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    // 如果写入的数据长度超过缓冲区剩余空间，则报错
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        va_end(arg_list);
        return false;
    }
    // 更新m_write_idx位置
    m_write_idx += len;
    // 清空可变参列表
    va_end( arg_list );
    return true;
}


bool http_conn::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers(size_t content_len) {
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();

    return false;
}

// 添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close" );
}

// 添加空行
bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

// 添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(size_t content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

// 添加文本类型，这里是html
bool http_conn::add_content_type() {
    // 这里目前只写了html 一种类型
     return add_response("Content-Type:%s\r\n", "text/html");
    // return add_response("Content-Type:%s\r\n", "application/json");

}

// 添加文本content
bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}



// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret)
    {
        case INTERNAL_ERROR:    // 内部错误
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title );
            add_headers(strlen(error_400_form));
            if ( ! add_content(error_400_form)) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:  // 没有足够权限
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:  // 请求文件成功
            add_status_line(200, ok_200_title );
            // 如果请求资源存在
            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                // 第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                // 第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                // 发送的全部数据为响应报文头部信息和文件大小
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                // 如果请求资源为0,返回空白html页面
                const char* ok_string = "<html><body></body><html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string))  return false;
            }
        default:
            return false;
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_res = process_read();
    if(read_res == NO_REQUEST) {    // 请求不完整
        mod_fd(m_epoll_fd,m_sock_fd,EPOLLIN); // 执行下一个读请求
        return;
    }

    // 生成响应
    bool write_ret = process_write(read_res);
    if(!write_ret) {
        close_conn();
    }
    mod_fd(m_epoll_fd,m_sock_fd,EPOLLOUT);
}
