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

//线程池支持的模式
enum class PoolMode {
	MODE_FIXED, //固定数量的线程
	MODE_CACHED //线程数量可动态增长
};

//线程类型
class Thread {
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;
	//线程构造
	Thread(ThreadFunc func)
		: func_(func)
		, threadId_(generateId_++) {
	}
	//线程析构
	~Thread() = default;
	//启动线程
	void start() {
		//创建一个线程来执行一个线程函数
		std::thread t(func_, threadId_); //对c++11来说， 线程对象t和线程函数func_
		t.detach(); //设置分离线程 
	}

	//获取线程id
	int getId() const {
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//保存线程id
};

int Thread::generateId_ = 0;

class ThreadPool {
public:
	//线程池构造
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

	//线程池析构
	~ThreadPool() {
		isPoolRunning_ = false;
		//等待线程池里面所有的线程返回
		// 两种状态： 阻塞  正在执行任务中
		//其实还有第3种 中间
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {
			return threads_.size() == 0;
			});
		std::cout << "All finished!\n";
	}

	//设置线程池的工作模式
	void setMode(PoolMode mode) {
		if (checkRunningState())
			return;
		poolMode_ = mode;
	}

	//设置初始的线程数量
	void setInitThreadSize(int size) {
		if (checkRunningState())
			return;
		initThreadSize_ = size;
	}

	//设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		taskQueMaxThreshHold_ = threshhold;
	}

	//设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold) {
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED) {
			threadSizeThreshHold_ = threshhold;
		}
	}

	//给线程池提交任务
	//使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	//Result submitTask(std::shared_ptr<Task> sp);
	//pool.submitTask(sum1, 10, 20)
	// 返回值future<> 
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
		//万能引用
		//打包任务，放入任务队列里面
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);
		std::future<RType> result = task->get_future();


		std::unique_lock<std::mutex> lock(taskQueMtx_);
		//用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败
		if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool
			{return taskQue_.size() < taskQueMaxThreshHold_; })) {
			//表示notFull_等待1s，条件依然没有满足
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType {return RType(); }//提交一个无意义的任务 作返回
			);
			(*task)();
			return task->get_future();
		}
		//如果有空余，把任务放入任务队列中
		// taskQue_.emplace(sp);
		// using Task = std::function<void()>;
		//函数参数不一致  中间套一层
		taskQue_.emplace(
			[task]() {
				//去执行下面的任务
				(*task)();
			}
		);
		taskSize_++;

		//因为新放了任务，任务队列肯定不空了，在notEmpty_上进行通知，赶快分配线程执行任务

		notEmpty_.notify_all();

		//cached模式 需要根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
		if (poolMode_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {
			//创建新线程
			/*auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
			threads_.emplace_back(std::move(ptr));
			curThreadSize_++;*/
			std::cout << ">>> create new thread ..." << std::endl;
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();//启动线程
			//修改线程个数相关的数量
			curThreadSize_++;
			idleThreadSize_++;
		}


		//返回任务的Result对象

		return result;
	}

	//开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency()) {
		isPoolRunning_ = true;
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;

		//创建线程对象
		for (int i = 0; i < initThreadSize_; i++) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			// threads_.emplace_back(std::move(ptr));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
		}

		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++) {
			threads_[i]->start(); //
			idleThreadSize_++; //记录初始空闲线程数量
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//定义线程函数
	void threadFunc(int threadid) {
		auto lastTime = std::chrono::high_resolution_clock().now();
		while (true) {
			//std::shared_ptr<Task> task;
			Task task;
			{
				//先获取锁
				std::unique_lock<std::mutex> lock(taskQueMtx_);
				std::cout << "tid: " << std::this_thread::get_id() << " 尝试获取任务...\n";

				//锁+双重判断
				while (taskQue_.size() == 0) {
					//线程池要结束，回收线程资源
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						std::cout << "threadid: " << std::this_thread::get_id() << " exit\n";
						exitCond_.notify_all();
						return;//线程函数结束 ，线程结束
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
				std::cout << "tid: " << std::this_thread::get_id() << " 获取任务成功...\n";

				//如果不空，从任务队列中取一个任务出来
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//如果依然有剩余任务，继续通知其他的线程执行任务
				if (taskQue_.size() > 0) {
					notEmpty_.notify_all();
				}

				//取出一个任务，得进行通知，通知可以继续提交生产任务
				notFull_.notify_all();

			}//就应该把锁释放掉
			std::cout << "tid: " << std::this_thread::get_id() << " begin !\n";
			//当前线程负责执行这个任务
			if (task != nullptr) {
				task(); // 执行function<void()>
			}
			std::cout << "tid: " << std::this_thread::get_id() << " end !\n";
			idleThreadSize_++;
			//更新线程执行完任务的时间
			lastTime = std::chrono::high_resolution_clock().now();
		}
	}

	//检查pool的运行状态
	bool checkRunningState() const {
		return isPoolRunning_;
	}
private:
	// std::vector<std::unique_ptr<Thread>> threads_; //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; //线程列表

	int initThreadSize_; //初始线程数量
	std::atomic_int curThreadSize_;// 记录当前线程池里面线程的总数量
	std::atomic_int idleThreadSize_;//记录空闲线程的数量
	int threadSizeThreshHold_; //线程数量上限阈值

	//std::queue<std::shared_ptr<Task>> taskQue_; //任务队列
	//Task任务 =》 函数对象
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;
	std::atomic_int taskSize_; //任务的数量
	int taskQueMaxThreshHold_; //任务队列数量上限阈值

	std::mutex taskQueMtx_; //保证任务队列的线程安全
	std::condition_variable notFull_; //表示任务队列不满
	std::condition_variable notEmpty_; //表示任务队列不空
	std::condition_variable exitCond_; //等待线程资源全部回收

	PoolMode poolMode_; //线程池工作模式

	//表示当前线程池的启动状态
	std::atomic_bool isPoolRunning_;
};

#endif // THREADPOOL_H
