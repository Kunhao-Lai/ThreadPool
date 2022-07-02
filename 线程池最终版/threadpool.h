#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<iostream>
#include<unordered_map>
#include<thread>
#include<future>

const int TASK_MAX_THRESHHOLD = 2;// INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 10;


//�̳߳�֧�ֵ�ģʽ 
enum class PoolMode {
	MODE_FIXED,  //�̶��������߳�
	MODE_CACHED, //�߳������ɶ�̬����
};

//�߳�����
class Thread {
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳���
	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{

	}
	//�߳�����
	~Thread() {}
	//�����߳�
	void start() {
		//����һ���߳���ִ��һ���̺߳���
		std::thread t(func_, threadId_);	//c++11��˵ �̶߳���t �̺߳���
		t.detach();		//���÷����߳�  pthread_detach pthread_t���óɷ����߳�
	}

	//��ȡ�߳�id
	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;	//�����߳�id
};

int Thread::generateId_ = 0;

//�̳߳�����
class ThreadPool {
public:
	//�̳߳ع���
	//�̳߳ع���
	ThreadPool()
		:initThreadSize_(0)
		, taskSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, curThreadSize_(0)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
		, idleThreadSize_(0) {

	}

	//�̳߳�����
	~ThreadPool() {
		isPoolRunning_ = false;


		//�ȴ��̳߳��������е��̷߳���  ������״̬������ & ����ִ��������
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	}

	
	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode) {
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//���������������������ֵ
	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	//����cachdģʽ���߳�����������ֵ
	void setThreadSizeThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThreshHold_ = threshhold;
		}

	}


	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���submitTask���Խ������������Ĳ��������⺯��
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args... args) -> std::future<decltype(func(args...))>
	{
		//������񣬷���������� 
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		//��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);

		//�߳�ͨ��  �ȴ���������п���
		//�û��ύ�����������������һ�룬�����ж��ύ����ʧ�ܣ�����
		if (!notFull_.wait_for(lock, std::chrono::seconds(1)
			, [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
		{
			//��ʾnotFull_�ȴ�1s��������Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); });
			(* task)();
			return task->get_future();
		}

		//����п��࣬������������������
		//taskQue_.emplace(sp);
		taskQue_.emplace([task]() {(*task)(); });
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
		return result;
	}

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
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

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid) {
		auto lastTime = std::chrono::high_resolution_clock().now();
		for (;;) {
			Task task;
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
				task();
			}
			idleThreadSize_++;
			//�����߳�ִ���������ʱ��
			lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	bool checkRunningState() const {
		return isPoolRunning_;
	}
private:
	//std::vector<std::unique_ptr<Thread>> threads_;  //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;  //�߳��б�

	size_t initThreadSize_;      //��ʼ���߳�����
	std::atomic_uint curThreadSize_;	//��¼��ǰ�̳߳������̵߳�������
	int threadSizeThreshHold_;	//�߳�����������ֵ
	std::atomic_uint idleThreadSize_;	//��¼�����̵߳�����

	//Task->��������
	using Task = std::function<void()>;
	std::queue<Task>  taskQue_;   //�������
	std::atomic_uint taskSize_;		//��������
	int taskQueMaxThreshHold_;		//�����������������ֵ

	std::mutex taskQueMtx_;		//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;	//��ʾ������в���
	std::condition_variable notEmpty_;	//��ʾ������в��� 
	std::condition_variable exitCond_;	//�ȴ��߳���Դȫ������

	PoolMode poolMode_;		//��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool  isPoolRunning_;	//��ʾ��ǰ�̳߳ص�����״̬

};





#endif // THREADPOOL_H
