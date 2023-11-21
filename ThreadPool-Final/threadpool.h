#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<iostream>
#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>
#include<thread>
#include<future>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode {
	MODE_FIXED, //�̶��������߳�
	MODE_CACHED //�߳������ɶ�̬����
};

//�߳�����
class Thread {
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;
	//�̹߳���
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++) {
	}
	//�߳�����
	~Thread() = default;
	//�����߳�
	void start() {
		//����һ���߳���ִ��һ���̺߳���
		std::thread t(func_, threadId_); //��c++11��˵�� �̶߳���t���̺߳���func_
		t.detach(); //���÷����߳� 
	}

	//��ȡ�߳�id
	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�id
};

int Thread::generateId_ = 0;

class ThreadPool {
public:
	//�̳߳ع���
	ThreadPool()
		: initThreadSize_(0)
		, taskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, isPoolRunning_(false)
	{}

	//�̳߳�����
	~ThreadPool() {
		isPoolRunning_ = false;
		//�ȴ��̳߳��������е��̷߳���
		// ����״̬�� ����  ����ִ��������
		//��ʵ���е�3�� �м�
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {
			return threads_.size() == 0;
			});
		std::cout << "All finished!\n";
	}

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode) {
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//���ó�ʼ���߳�����
	void setInitThreadSize(int size) {
		if (checkRunningState())
			return;
		initThreadSize_ = size;
	}

	//����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	//�����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThreshHold_ = threshhold;
		}
	}

	//���̳߳��ύ����
	//ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
	//Result submitTask(std::shared_ptr<Task> sp);
	//pool.submitTask(sum1, 10, 20)
	// ����ֵfuture<> 
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		//��������
		//������񣬷��������������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);
		std::future<RType> result = task->get_future();


		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//�û��ύ�����������������1s�������ж��ύ����ʧ��
		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool
			{return taskQue_.size() < taskQueMaxThreshHold_; })) {
			//��ʾnotFull_�ȴ�1s��������Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); }//�ύһ������������� ������
			);
			(*task)();
			return task->get_future();
		}
		//����п��࣬������������������
		// taskQue_.emplace(sp);
		// using Task = std::function<void()>;
		//����������һ��  �м���һ��
		taskQue_.emplace(
			[task]() {
				//ȥִ�����������
				(*task)();
			}
		);
		taskSize_++;

		//��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������

		notEmpty_.notify_all();

		//cachedģʽ ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {
			//�������߳�
			/*auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
			threads_.emplace_back(std::move(ptr));
			curThreadSize_++;*/
			std::cout << ">>> create new thread ..." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//�����߳�
			//�޸��̸߳�����ص�����
			curThreadSize_++;
			idleThreadSize_++;
		}


		//���������Result����

		return result;
	}

	//�����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
		isPoolRunning_ = true;
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//�����̶߳���
		for (int i = 0; i < initThreadSize_; i++) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			// threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//���������߳�
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start(); //
			idleThreadSize_++; //��¼��ʼ�����߳�����
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//�����̺߳���
	void threadFunc(int threadid) {
		auto lastTime = std::chrono::high_resolution_clock().now();
		while (true) {
			//std::shared_ptr<Task> task;
			Task task;
			{
				//�Ȼ�ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid: " << std::this_thread::get_id() << " ���Ի�ȡ����...\n";

				//��+˫���ж�
				while (taskQue_.size() == 0) {
					//�̳߳�Ҫ�����������߳���Դ
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						std::cout << "threadid: " << std::this_thread::get_id() << " exit\n";
						exitCond_.notify_all();
						return;//�̺߳������� ���߳̽���
					}

					if (poolMode_ == PoolMode::MODE_CACHED) {
						if (std::cv_status::timeout ==
							notEmpty_.wait_for(lock, std::chrono::seconds(1))
							) {
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								threads_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid: " << std::this_thread::get_id() << " exit\n";
								return;
							}
						}
					}
					else {
						notEmpty_.wait(lock);
					}
				}

				idleThreadSize_--;
				std::cout << "tid: " << std::this_thread::get_id() << " ��ȡ����ɹ�...\n";

				//������գ������������ȡһ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//�����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}

				//ȡ��һ�����񣬵ý���֪ͨ��֪ͨ���Լ����ύ��������
				notFull_.notify_all();

			}//��Ӧ�ð����ͷŵ�
			std::cout << "tid: " << std::this_thread::get_id() << " begin !\n";
			//��ǰ�̸߳���ִ���������
			if (task != nullptr) {
				task(); // ִ��function<void()>
			}
			std::cout << "tid: " << std::this_thread::get_id() << " end !\n";
			idleThreadSize_++;
			//�����߳�ִ���������ʱ��
			lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//���pool������״̬
	bool checkRunningState() const {
		return isPoolRunning_;
	}
private:
	// std::vector<std::unique_ptr<Thread>> threads_; //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; //�߳��б�

	int initThreadSize_; //��ʼ�߳�����
	std::atomic_int curThreadSize_;// ��¼��ǰ�̳߳������̵߳�������
	std::atomic_int idleThreadSize_;//��¼�����̵߳�����
	int threadSizeThreshHold_; //�߳�����������ֵ

	//std::queue<std::shared_ptr<Task>> taskQue_; //�������
	//Task���� =�� ��������
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;
	std::atomic_int taskSize_; //���������
	int taskQueMaxThreshHold_; //�����������������ֵ

	std::mutex taskQueMtx_; //��֤������е��̰߳�ȫ
	std::condition_variable notFull_; //��ʾ������в���
	std::condition_variable notEmpty_; //��ʾ������в���
	std::condition_variable exitCond_; //�ȴ��߳���Դȫ������

	PoolMode poolMode_; //�̳߳ع���ģʽ

	//��ʾ��ǰ�̳߳ص�����״̬
	std::atomic_bool isPoolRunning_;
};

#endif // THREADPOOL_H
