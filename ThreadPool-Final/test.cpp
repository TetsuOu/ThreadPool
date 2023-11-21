
#include<iostream>
#include<functional>
#include<future>

#include "threadpool.h"

using namespace std;
/*
������̳߳��ύ������ӷ���
1. pool.submitTask(sum1, 10, 20)
   pool.submitTask(sum2, 1, 2, 3)
   submitTask: �ɱ��ģ����

2. �滻�Լ�ʵ�ֵ�Result��Any����
   ʹ��future������Result��ʡ�̳߳ش���

*/

int sum1(int a, int b) {
	return a + b;
}

int sum2(int a, int b, int c) {
	return a + b + c;
}

int main() {
	//packaged_task<int(int, int)> task(sum1);

	//future<int> res = task.get_future();
	//// task(10, 20);

	//thread t(move(task), 10, 20);
	//t.detach();

	//cout << res.get() << endl;

	ThreadPool pool;
	pool.start(2);

	future<int> r1 = pool.submitTask(sum1, 1, 2);
	future<int> r2 = pool.submitTask(sum1, 1, 2);
	future<int> r3 = pool.submitTask([](int s, int e) {
		int sum = 0;
		for (int i = s; i <= e; i++) {
			sum += i;
		}
		return sum;
		}, 1, 1000);
	future<int> r4 = pool.submitTask(sum1, 1, 2);
	future<int> r5 = pool.submitTask(sum1, 1, 2);
	cout << r1.get()<<endl;
	cout << r2.get() << endl;
	cout << r3.get() << endl;
	cout << r4.get() << endl;
	cout << r5.get() << endl;

	//getchar();
}