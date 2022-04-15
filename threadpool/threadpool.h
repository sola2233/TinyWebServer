/**
 * 线程池结构
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

// 线程池类，定义成模板类是为了代码的复用，模板参数 T 就是任务类
template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 请求队列
    locker m_queuelocker;        // 保护请求队列的互斥锁
    sem m_queuestat;             // 是否有任务需要处理
    connection_pool *m_connPool; // 数据库
    int m_actor_model;           // 模型切换
};

// 构造函数
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : 
    m_actor_model(actor_model), 
    m_thread_number(thread_number), 
    m_max_requests(max_requests), 
    m_threads(NULL), 
    m_connPool(connPool)
{
    // 防止数量小于 0
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 描述线程池的数组
    m_threads = new pthread_t[m_thread_number];
    // 没创建成功则抛出异常
    if (!m_threads)
        throw std::exception();
    // 创建 thread_number 个线程，并将它们设置为线程分离（detach）
    for (int i = 0; i < thread_number; ++i)
    {
        // work 是 threadpool 类的一个静态成员函数
        // 因为静态成员函数不能访问非静态成员，所以将对象的 this 指针传进去作为参数
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// 向队列中添加任务
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)   // 如果超出最大任务量
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    // 添加到任务队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 信号量增加
    m_queuestat.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 类对象的 this 指针作为参数被传递进来
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;    // 返回值没什么用
}

// 线程池运行起来做的事情
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        // 信号量有值就不阻塞，信号量值减一
        // 没有值就阻塞，判断有没有任务做
        m_queuestat.wait();
        m_queuelocker.lock();
        // 判断有没有任务做
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        // 获取第一个任务
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock(); // 解锁

        // 判断有没有获取到任务
        if (!request)
            continue;
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
