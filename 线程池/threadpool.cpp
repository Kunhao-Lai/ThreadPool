#include"threadpool.h"
#include<functional>
#include<thread>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;

//�̳߳ع���
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThreadSize_(0)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false) 
	, idleThreadSize_(0) {

}

//�̳߳ص�����
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	

	//�ȴ��̳߳��������е��̷߳���  ������״̬������ & ����ִ��������
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

//���̳߳��ύ����  �û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//�߳�ͨ��  �ȴ���������п���
	//�û��ύ�����������������һ�룬�����ж��ύ����ʧ�ܣ�����
	if (!notFull_.wait_for(lock, std::chrono::seconds(1)
		, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		//��ʾnotFull_�ȴ�1s��������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}
	
	//����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��Ϊ����������������п϶����գ���notEmpty_֪ͨ,�Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	//cachedģʽ ������ȽϽ��� ������С���������  
	//��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& threadSizeThreshHold_ > curThreadSize_) {

		std::cout << ">>> create new thread" << std::endl;
		//�������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//�����߳�
		threads_[threadId]->start();
		//�޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++;
	}

	//���������Result����
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) {
	//�����̳߳ص�����״̬
	isPoolRunning_ = true;

	//��¼�̳߳�ʼ����
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	//�����̶߳���
	for (int i = 0; i < initThreadSize_; i++) {
		//����thread�̶߳����ʱ�򣬰��̺߳�������thread����
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		//threads_.emplace_back(std::move(ptr));
	}

	//���������߳�
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;	//��¼��ʼ�����̵߳�����
	}
}

//�����̺߳���  �̳߳ص����к����������������������
void ThreadPool::threadFunc(int threadid) {		
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ���Ի�ȡ����..." << std::endl;

			//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s
			//,��ô�ѿ��е��̻߳��գ�(����initThreadSize_�������߳�Ҫ����)
			//��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� �� 60s
			 
			//ÿһ���з���һ��
			//��ô���֣���ʱ���أ������������ִ�з���
			while (taskQue_.size() == 0) {
				//�̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_) {
					threads_.erase(threadid);
					std::cout << "threadid: " << std::this_thread::get_id()
						<< " exit!" << std::endl;
					exitCond_.notify_all();
					return;		//�̺߳����������߳̽���
				}
				if (poolMode_ == PoolMode::MODE_CACHED) {
					//����������ʱ����
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_) {
							//��ʼ���յ�ǰ�߳�
							//��¼�߳���������ر�����ֵ���޸�
							//���̶߳�����߳��б���ɾ��
							//threadid => thread���� => ɾ��
							threads_.erase(threadid);
							idleThreadSize_--;
							curThreadSize_--;

							std::cout << "threadid: " << std::this_thread::get_id()
								<< " exit!" << std::endl;

							return;
						}
					}
				}
				else {
					//�ȴ�notEmpty����
					notEmpty_.wait(lock);
				}

				//�̳߳�Ҫ�����������߳���Դ
				/*if (!isPoolRunning_) {
					threads_.erase(threadid);

					std::cout << "threadid: " << std::this_thread::get_id()
						<< " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}*/
			}
			

			idleThreadSize_--;

			std::cout << "tid: " << std::this_thread::get_id()
				<< " ��ȡ����ɹ�..." << std::endl;

			//�����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			//�����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}

			//ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}  //���������ͷ�����ִ���Լ�������
		
		//��ǰ�̸߳���ִ���������
		if (task != nullptr) {
			//task->run();	//1��ִ������  2����������ķ���ֵ
			task->exec();
		}
		idleThreadSize_++;
		//�����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
	}

	
}

////////////////// <Task����ʵ��>///
Task::Task() :result_(nullptr) {

}

Task::~Task(){

}
void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run());		//���﷢����̬����
	}
}
void Task::setResult(Result* res) {
	result_ = res;
}

////////////////// <�̷߳���ʵ��>///
int Thread::generateId_ = 0;


//�̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++)
{

}
//�߳�����
Thread::~Thread() {

}


//�����߳�
void Thread::start() {
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_);	//c++11��˵ �̶߳���t �̺߳���
	t.detach();		//���÷����߳�  pthread_detach pthread_t���óɷ����߳�
}

//��ȡ�߳�id
int Thread::getId() const {
	return threadId_;
}

/// </�̷߳���ʵ��>


/////////////   Result������ʵ��
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