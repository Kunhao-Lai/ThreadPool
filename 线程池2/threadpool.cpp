#include"threadpool.h"

const int THREAD_MAX_THRESHHOLD = 100;
const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_IDLE_TIME = 60;

//////////Result方法/////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:isValid_(isValid)
	, task_(task) {
	task_->setResult(this);
}

Any Result::get() {	//用户调用的
	if (!isValid_) {
		return "";
	}
	sem_.wait();  //任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}

void Result::setVal(Any any) {
	//存储task的返回值
	any_ = std::move(any);
	sem_.post();	//已经获取了任务的返回值，增加信号量资源
}

//////////任务方法///////////////
Task::Task() :result_(nullptr) {

}

Task::~Task() {

}
void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run());		//这里发生多态调用
	}
}
void Task::setResult(Result* res) {
	result_ = res;
}


//////////线程方法//////////////

int Thread::generateId_ = 0;

//线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++) {

}

//线程析构
Thread::~Thread() {

}

//线程启动
void Thread::start() {
	//创建一个线程来执行一个线程函数
	std::thread t(func_, threadId_);
	//设置成分离线程
	t.detach();
}

//获取线程ID
int Thread::getId() const {
	return threadId_;
}

//////////线程池方法//////////////

//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThreadSize_(0)
	, idleThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, isPoolRunning_(false)
	, poolMode_(PoolMode::MODE_FIXED) {

}


//线程池析构
ThreadPool::~ThreadPool() {
	//首先设置线程池运行状态
	isPoolRunning_ = false;

	//线程有两个状态  阻塞和运行  通知阻塞的线程起来
	
	//等待线程结束
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

//设置任务队列数量上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

//设置cachd模式下线程数量上限阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}

}


//启动线程池
void ThreadPool::start(int initThreadSize) {

	isPoolRunning_ = true;

	//记录线程初始个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize_;

	//1、创建线程对象
	for (int i = 0; i < initThreadSize_; ++i) {
		//创建线程对象的时候，把线程函数给到thread对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));
		//	threads_.emplace_back( std::move(ptr));
	}

	//2、启动所有线程
	for (int i = 0; i < initThreadSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

//给线程池提交任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//上锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//检测任务队列是否满了，等待10s，还满就输出提交失败
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; })) {
		std::cerr << "task queue full,submit task fail." << std::endl;
		return Result(sp, false);
	}

	//放入队列，通知任务队列不为空
	taskQue_.emplace(sp);
	taskSize_++;
	notEmpty_.notify_all();

	if (PoolMode::MODE_CACHED == poolMode_
		&& curThreadSize_ < threadSizeThreshHold_
		&& idleThreadSize_ < taskSize_) {
		std::cout << ">>> create new thread" << std::endl;
		//创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,
			this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));

		//启动新线程
		threads_[threadid]->start();
		idleThreadSize_++;

		//修改相关变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	return Result(sp);
}

//定义线程函数 线程池的所有函数从任务队列装中消费任务
/*
//死锁情景下的代码
void ThreadPool::threadFunc(int threadid) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (isPoolRunning_ == true) {

		std::shared_ptr<Task> task;
		{
			//获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " 尝试获取任务..." << std::endl;

			//cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s
			//怎么把空闲的线程回收？(超过initThreadSize_数量的线程要回收)
			//当前时间 - 上一次线程执行的时间 》 60s
			while (taskSize_ == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//开始回收线程
							threads_.erase(threadid);

							//修改相关计数
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id()
								<< " exit!" << std::endl;

							return;
						}
					}
				}
				else {
					//等待任务队列有任务
					notEmpty_.wait(lock);
				}
				if (isPoolRunning_ == false) {
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id()
						<< " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}
			}

			idleThreadSize_--;

			//取出任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< " 获取任务成功..." << std::endl;

			//如果任务数量还大于0，继续通知其他线程
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//通知任务队列不满
			notFull_.notify_all();
		}
		if (task != nullptr) {
			//开始执行任务
			task->exec();
		}
		idleThreadSize_++;

		//更新线程执行完任务的时间
		lastTime = std::chrono::high_resolution_clock().now();

	}
	threads_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id()
		<< " exit!" << std::endl;
	exitCond_.notify_all();
}
*/
//死锁修改后的代码
void ThreadPool::threadFunc(int threadid) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (isPoolRunning_ == true) {

		std::shared_ptr<Task> task;
		{
			//获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " 尝试获取任务..." << std::endl;

			//锁 + 双重判断
			while (isPoolRunning_&& taskSize_ == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//开始回收线程
							threads_.erase(threadid);

							//修改相关计数
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id()
								<< " exit!" << std::endl;

							return;
						}
					}
				}
				else {
					//等待任务队列有任务
					notEmpty_.wait(lock);
				}
				
			}
			if (isPoolRunning_ == false) {
				break;
			}
			idleThreadSize_--;

			//取出任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< " 获取任务成功..." << std::endl;

			//如果任务数量还大于0，继续通知其他线程
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//通知任务队列不满
			notFull_.notify_all();
		}
		if (task != nullptr) {
			//开始执行任务
			task->exec();
		}
		idleThreadSize_++;

		//更新线程执行完任务的时间
		lastTime = std::chrono::high_resolution_clock().now();

	}
	threads_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id()
		<< " exit!" << std::endl;
	exitCond_.notify_all();
}