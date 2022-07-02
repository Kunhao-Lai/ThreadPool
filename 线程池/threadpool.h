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

//Any���ͣ����Խ����������ݵ�����
class Any {
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	//������캯��������Any���ͽ����������������
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) {

	}

	//��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_() {
		//��ô��base_�����ҵ���ָ������������ȡ���������data��Ա����
		//����ָ�� =�� ������ָ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw "Type is unmatch!";
		}
		return pd->data_;
	}
private:
	//��������
	class Base {
	public:
		virtual ~Base() = default;
	};

	//����������
	template<typename T>
	class Derive :public Base{
	public:
		Derive(T data) :data_(data) {}
		T data_;	
	};
private:
	//����һ�������ָ��
	std::unique_ptr<Base> base_;
};

//ʵ��һ���ź���
class Semaphore {
public:
	Semaphore(int limit = 0) :resLimit_(0), isExit_(false) {

	}
	~Semaphore() {
		isExit_ = true;
	}

	//��ȡһ���ź�����Դ
	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}
	//����һ���ź�����Դ
	void post() {
		if (isExit_)
			return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		//linux��condition_variable����������ʲôҲû��
		//��������״̬ʧЧ���޹�����
		cond_.notify_all();
	}
private:
	std::atomic_bool isExit_;
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

//Task���͵�ǰ������
class Task;

//ʵ�ֽ��������ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//����һ��setVal��������ȡ����ִ����ķ���ֵ
	void setVal(Any any);
	//�������get�������û��������������ȡtask�ĵķ���ֵ
	Any get();
private:
	Any any_;	//�洢����ķ���ֵ
	Semaphore sem_;		//�߳�ͨ���ź���
	std::shared_ptr<Task> task_;	//ָ���ȡ��Ӧ����ֵ���������
	std::atomic_bool isValid_;		//����ֵ�Ƿ���Ч
};

//����������
//�û������Զ����������ͣ���Task�̳У���дrun������ʵ���Զ���������
class Task {
public:
	Task();
	~Task();
	void exec();
	void setResult(Result* res);
	//�û������Զ����������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;
private:
	Result* result_;
};

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
	Thread(ThreadFunc func);
	//�߳�����
	~Thread();
	//�����߳�
	void start();

	//��ȡ�߳�id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;	//�����߳�id
}; 

/*
example:
ThreadPool pool;
pool.start();

class MyTask{
public:
	void run(){//�̴߳���....}
};

pool.submitTask(std::make_shared<MyTask>());
*/


//�̳߳�����
class ThreadPool {
public:
	//�̳߳ع���
	ThreadPool();

	//�̳߳�����
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//���������������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	//����cachdģʽ���߳�����������ֵ
	void setThreadSizeThreshHold(int threshhold);

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	//�����̺߳���
	void threadFunc(int threadid);

	//���pool������״̬
	bool checkRunningState() const;
private:
	//std::vector<std::unique_ptr<Thread>> threads_;  //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;  //�߳��б�
	
	size_t initThreadSize_;      //��ʼ���߳�����
	std::atomic_uint curThreadSize_;	//��¼��ǰ�̳߳������̵߳�������
	int threadSizeThreshHold_;	//�߳�����������ֵ
	std::atomic_uint idleThreadSize_;	//��¼�����̵߳�����

	std::queue<std::shared_ptr<Task>>  taskQue_;   //�������
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
