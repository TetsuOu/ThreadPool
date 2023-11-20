#include "threadpool.h"

#include<thread>
#include<functional>
#include<iostream>
const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10;

//�̳߳ع���
ThreadPool::ThreadPool()
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
ThreadPool::~ThreadPool() {
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
void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

//���ó�ʼ���߳�����
void ThreadPool::setInitThreadSize(int size) {
	if (checkRunningState())
		return;
	initThreadSize_ = size;
}

//����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold) {
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED) {
		threadSizeThreshHold_ = threshhold;
	}
}

//���̳߳��ύ���� 
Result ThreadPool::submitTask(std::shared_ptr<Task> sp) {
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	//�̵߳�ͨ�� �ȴ���������п��� wait wait_for wait_unitl
	/*while (taskQue_.size() == taskQueMaxThreshHold_) {
		notFull_.wait(lock);
	}*/

	//ֻҪ��������ͷ��أ�
	/*notFull_.wait(lock, [&]()->bool
		{return taskQue_.size() < taskQueMaxThreshHold_; });*/

		//�û��ύ�����������������1s�������ж��ύ����ʧ��
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool
		{return taskQue_.size() < taskQueMaxThreshHold_; })) {
		//��ʾnotFull_�ȴ�1s��������Ȼû������
		std::cerr << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}


	//����п��࣬������������������
	taskQue_.emplace(sp);
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

	return Result(sp, true);
}

//�����̳߳�
void ThreadPool::start(int initThreadSize) {
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

//�����̺߳���  �̳߳ص������̴߳��������������������
void ThreadPool::threadFunc(int threadid) { //�̺߳������أ���Ӧ���߳̽���
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (true) {
		std::shared_ptr<Task> task;
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
			// task->run(); //ִ�����񣻰�����ķ���ֵsetVal����Result
			task->exec();
		}
		std::cout << "tid: " << std::this_thread::get_id() << " end !\n";
		idleThreadSize_++;
		//�����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
	}

	
}

bool ThreadPool::checkRunningState() const {
	return isPoolRunning_;
}

//�̷߳���ʵ��
int Thread::generateId_ = 0;

//�̹߳���
Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++) {

}
//�߳�����
Thread::~Thread() {

}

//�����߳�
void Thread::start() {
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_); //��c++11��˵�� �̶߳���t���̺߳���func_
	t.detach(); //���÷����߳� 
}

int Thread::getId() const {
	return threadId_;
}


//Task ����ʵ��
Task::Task()
	: result_(nullptr) {
}

void Task::exec() {
	if (result_ != nullptr) {
		// run(); //���﷢����̬����
		result_->setVal(run());
	}
}

void Task::setResult(Result* res) {
	result_ = res;
}

// Result ����ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task) {
	task_->setResult(this);
}

Any Result::get() { //�û�����
	if (!isValid_) {
		return "";
	}
	sem_.wait(); //task�������û��ִ���꣬����������û����߳�
	return std::move(any_);
}

void Result::setVal(Any any) { //
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();//�Ѿ���ȡ������ķ���ֵ�������ź�����Դ
}