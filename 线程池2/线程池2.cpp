// threadpool.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//
#include"threadpool.h"
#include <iostream>
#include <vector>
#include<functional>
#include<thread>
#include<chrono>

using namespace std;

/*
class Thread {
public:
    Thread(function<void(int)> func, int no) :_func(func), _no(no) {}
    thread start() {
        thread t(_func, _no);
        return t;
    }
private:
    int _no;
    function<void(int)> _func;
};

class ThreadPool {
public:
    ThreadPool() {}
    ~ThreadPool() {
        for (int i = 0; i < _pool.size(); ++i) {
            delete _pool[i];
        }
    }
    void startPool(int size) {
        //创建线程
        for (int i = 0; i < size; ++i) {
            _pool.push_back(
                new Thread(bind(&ThreadPool::threadRun, this,placeholders::_1), i));
        }

        //启动线程
        for (int i = 0; i < size; ++i) {
            _handler.push_back(_pool[i]->start());
        }

        for (auto& t : _handler) {
            t.join();
        }
    }
private:
    vector<Thread*> _pool;
    vector<thread> _handler;
    void threadRun(int i) {
        cout << "threadRun id: " << i << endl;
    }
};
*/

using uLong = unsigned long long;

class MyTask :public Task {
public:
    MyTask(int begin, int end) :begin_(begin), end_(end) {}
    ~MyTask() {}
    //问题一：怎么设计run函数的返回值，可以表示任何的类型
    //java python  Object是其它所有类类型的基类
    //C++17   Any类型
    Any run() {
        std::cout << "tid: " << this_thread::get_id() << " begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++)
            sum += i;
        //   std::cout << "tid: " << this_thread::get_id() << " count:" << sum << std::endl;
        std::cout << "tid: " << this_thread::get_id() << " end!" << std::endl;

        return sum;
    }
private:
    int begin_;
    int end_;
};

int main()
{
    //ThreadPool对象析构之后，怎么回收资源
    {
        ThreadPool pool;
        //用户自己设置线程池的工作模式
        pool.setMode(PoolMode::MODE_CACHED);

        //开始启动线程池
        pool.start(4);

        //如何设置这里的Result机制呢？
        Result res1 = pool.submitTask(std::make_shared<MyTask>(0, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        uLong sum1 = res1.get().cast_<uLong>();   //get返回的Any类型转换成具体的类型
        uLong sum2 = res2.get().cast_<uLong>();

        //Master - Slave线程模型
        //Master线程用来分解任务，然后给各个Salve线程分配任务
        //等待各个Salve线程执行完任务，返回结果
        //Master线程合并各个任务结果，输出
        cout << (sum1 + sum2) << endl;

        uLong sum = 0;
        for (uLong i = 0; i <= 300000000; i++)
            sum += i;
        cout << sum << endl;
    }
    getchar();
    return 0;
}

