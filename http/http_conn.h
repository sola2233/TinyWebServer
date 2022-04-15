#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区的大小

    // HTTP 请求的方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    // 解析客户端请求时，主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,    // 正在分析请求行
        CHECK_STATE_HEADER,             // 正在分析头部字段
        CHECK_STATE_CONTENT             // 正在解析请求体
    };

    // 服务器处理 HTTP 请求的可能结果，报文解析的结果
    enum HTTP_CODE
    {
        NO_REQUEST,         // 请求不完整，需要继续读取客户数据
        GET_REQUEST,        // 获得了一个完成的客户请求
        BAD_REQUEST,        // 客户请求语法错误
        NO_RESOURCE,        // 服务器没有资源
        FORBIDDEN_REQUEST,  // 客户对资源没有足够的访问权限
        FILE_REQUEST,       // 文件请求，获取文件成功
        INTERNAL_ERROR,     // 服务器内部错误
        CLOSED_CONNECTION   // 客户端已经关闭连接了
    };

    // 从状态机的三种可能状态，即行的读取状态
    enum LINE_STATUS
    {
        LINE_OK = 0,    // 读取到一个完整的行
        LINE_BAD,       // 行出错
        LINE_OPEN       // 行数据尚且不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);   // 解析请求行首行
    HTTP_CODE parse_headers(char *text);        // 解析头部
    HTTP_CODE parse_content(char *text);        // 解析请求体
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];  // 读缓冲的数组
    int m_read_idx;                     // 标示读缓冲区中已经读入的客户端数据的
                                        // 最后一个字节的下一个位置
    int m_checked_idx;                  // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;                   // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];// 写缓冲的数组
    int m_write_idx;
    CHECK_STATE m_check_state;          // 主状态机当前所处的状态
    char m_real_file[FILENAME_LEN];
    // 请求行字段
    METHOD m_method;                    // 请求方法
    char *m_url;                        // 请求目标的文件名
    char *m_version;                    // 协议版本，只支持 HTTP1.1
    // 首部字段
    char *m_host;                       // 主机名
    bool m_linger;                      // HTTP 请求是否要保持连接
    int m_content_length;
    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
