#include"threadpool.h"

const int THREAD_MAX_THRESHHOLD = 100;
const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_IDLE_TIME = 60;

//////////Result����/////////////
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:isValid_(isValid)
	, task_(task) {
	task_->setResult(this);
}

Any Result::get() {	//�û����õ�
	if (!isValid_) {
		return "";
	}
	sem_.wait();  //�������û��ִ���꣬����������û����߳�
	return std::move(any_);
}

void Result::setVal(Any any) {
	//�洢task�ķ���ֵ
	any_ = std::move(any);
	sem_.post();	//�Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}

//////////���񷽷�///////////////
Task::Task() :result_(nullptr) {

}

Task::~Task() {

}
void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run());		//���﷢����̬����
	}
}
void Task::setResult(Result* res) {
	result_ = res;
}


//////////�̷߳���//////////////

int Thread::generateId_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++) {

}

//�߳�����
Thread::~Thread() {

}

//�߳�����
void Thread::start() {
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_);
	//���óɷ����߳�
	t.detach();
}

//��ȡ�߳�ID
int Thread::getId() const {
	return threadId_;
}

//////////�̳߳ط���//////////////

//�̳߳ع���
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


//�̳߳�����
ThreadPool::~ThreadPool() {
	//���������̳߳�����״̬
	isPoolRunning_ = false;

	//�߳�������״̬  ����������  ֪ͨ�������߳�����
	
	//�ȴ��߳̽���
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

//���������������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

//����cachdģʽ���߳�����������ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}

}


//�����̳߳�
void ThreadPool::start(int initThreadSize) {

	isPoolRunning_ = true;

	//��¼�̳߳�ʼ����
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize_;

	//1�������̶߳���
	for (int i = 0; i < initThreadSize_; ++i) {
		//�����̶߳����ʱ�򣬰��̺߳�������thread����
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));
		//	threads_.emplace_back( std::move(ptr));
	}

	//2�����������߳�
	for (int i = 0; i < initThreadSize_; ++i) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}

//���̳߳��ύ����
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//����
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�����������Ƿ����ˣ��ȴ�10s������������ύʧ��
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; })) {
		std::cerr << "task queue full,submit task fail." << std::endl;
		return Result(sp, false);
	}

	//������У�֪ͨ������в�Ϊ��
	taskQue_.emplace(sp);
	taskSize_++;
	notEmpty_.notify_all();

	if (PoolMode::MODE_CACHED == poolMode_
		&& curThreadSize_ < threadSizeThreshHold_
		&& idleThreadSize_ < taskSize_) {
		std::cout << ">>> create new thread" << std::endl;
		//�������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,
			this, std::placeholders::_1));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));

		//�������߳�
		threads_[threadid]->start();
		idleThreadSize_++;

		//�޸���ر���
		curThreadSize_++;
		idleThreadSize_++;
	}

	return Result(sp);
}

//�����̺߳��� �̳߳ص����к������������װ����������
/*
//�����龰�µĴ���
void ThreadPool::threadFunc(int threadid) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (isPoolRunning_ == true) {

		std::shared_ptr<Task> task;
		{
			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ���Ի�ȡ����..." << std::endl;

			//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s
			//��ô�ѿ��е��̻߳��գ�(����initThreadSize_�������߳�Ҫ����)
			//��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� �� 60s
			while (taskSize_ == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//��ʼ�����߳�
							threads_.erase(threadid);

							//�޸���ؼ���
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id()
								<< " exit!" << std::endl;

							return;
						}
					}
				}
				else {
					//�ȴ��������������
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

			//ȡ������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ��ȡ����ɹ�..." << std::endl;

			//�����������������0������֪ͨ�����߳�
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//֪ͨ������в���
			notFull_.notify_all();
		}
		if (task != nullptr) {
			//��ʼִ������
			task->exec();
		}
		idleThreadSize_++;

		//�����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();

	}
	threads_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id()
		<< " exit!" << std::endl;
	exitCond_.notify_all();
}
*/
//�����޸ĺ�Ĵ���
void ThreadPool::threadFunc(int threadid) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (isPoolRunning_ == true) {

		std::shared_ptr<Task> task;
		{
			//��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ���Ի�ȡ����..." << std::endl;

			//�� + ˫���ж�
			while (isPoolRunning_&& taskSize_ == 0) {
				if (poolMode_ == PoolMode::MODE_CACHED) {
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//��ʼ�����߳�
							threads_.erase(threadid);

							//�޸���ؼ���
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id()
								<< " exit!" << std::endl;

							return;
						}
					}
				}
				else {
					//�ȴ��������������
					notEmpty_.wait(lock);
				}
				
			}
			if (isPoolRunning_ == false) {
				break;
			}
			idleThreadSize_--;

			//ȡ������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ��ȡ����ɹ�..." << std::endl;

			//�����������������0������֪ͨ�����߳�
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//֪ͨ������в���
			notFull_.notify_all();
		}
		if (task != nullptr) {
			//��ʼִ������
			task->exec();
		}
		idleThreadSize_++;

		//�����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();

	}
	threads_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id()
		<< " exit!" << std::endl;
	exitCond_.notify_all();
}