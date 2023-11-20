#include<iostream>
#include<chrono>
#include "threadpool.h"
using namespace std;
using uLong = unsigned long long;
class MyTask : public Task {
public:
	MyTask(int begin, int end) {
		begin_ = begin;
		end_ = end;
	}
	Any run() {
		/*std::cout << "begin threadFunc tid: "<< std::this_thread::get_id() << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(2));
		std::cout << "end threadFunc tid: " << std::this_thread::get_id() << std::endl;

		return 1;*/
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++) {
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return sum;
	}
private:
	int begin_;
	int end_;
};
int main() {
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(2);
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(10001, 20000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(20001, 30000));
		uLong sum1 = res1.get().cast_<uLong>();

		std::cout << sum1 << endl;
	}

	std::cout << "main over! \n";

	//getchar();
	//{
	//	ThreadPool pool;
	//	pool.setMode(PoolMode::MODE_CACHED);
	//	pool.start(4);

	//	//std::this_thread::sleep_for(std::chrono::seconds(5));

	//	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 10000));
	//	Result res2 = pool.submitTask(std::make_shared<MyTask>(10001, 20000));
	//	Result res3 = pool.submitTask(std::make_shared<MyTask>(20001, 30000));

	//	pool.submitTask(std::make_shared<MyTask>(20001, 30000));
	//	pool.submitTask(std::make_shared<MyTask>(20001, 30000));
	//	pool.submitTask(std::make_shared<MyTask>(20001, 30000));

	//	uLong sum1 = res1.get().cast_<uLong>();
	//	uLong sum2 = res2.get().cast_<uLong>();
	//	uLong sum3 = res3.get().cast_<uLong>();

	//	cout << (sum1 + sum2 + sum3) << endl;
	//}

	//getchar();
}