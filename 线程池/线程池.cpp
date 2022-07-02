// 线程池.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include<chrono>
#include<thread>
#include"threadpool.h"
using namespace std;

/*
有些场景，是希望获取线程执行任务的返回值的
*/
using uLong = unsigned long long;

class MyTask :public Task{
public:
    MyTask(int begin, int end) :begin_(begin), end_(end) {}
    ~MyTask() {}
    Any run() {
        std::cout << "tid: " << this_thread::get_id() << " begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
     
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
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_FIXED);
        //开始启动线程池
        pool.start(4);
        //res也是局部对象要析构的
        Result res1 = pool.submitTask(std::make_shared<MyTask>(0, 100000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        
    }//这里result对象也要析构
    std::cout << "main over" << endl;

#if 0
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
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        uLong sum1 = res1.get().cast_<uLong>();   //get返回的Any类型转换成具体的类型
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();

        //Master - Slave线程模型
        //Master线程用来分解任务，然后给各个Salve线程分配任务
        //等待各个Salve线程执行完任务，返回结果
        //Master线程合并各个任务结果，输出
        cout << (sum1 + sum2 + sum3) << endl;

        uLong sum = 0;
        for (uLong i = 0; i <= 300000000; i++)
            sum += i;
        cout << sum << endl;
    }
    getchar();
#endif
    
    getchar();
    return 0;
}

