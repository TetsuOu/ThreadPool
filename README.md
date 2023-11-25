# ThreadPool

## 环境

Visual Studio 2022

C++  latest 



## linux编译

```bash
g++ -fPIC -shared threadpool.cpp -o libtdpool.so -std=c++17
mv libtdpool.so /usr/local/lib
mv threadpool.h /usr/local/include/
g++ test.cpp -o test -std=c++17 -ltdpool -lpthread

cd /etc/ld.so.conf.d/
vim mylib.conf  # INSERT /usr/local/lib

ldconfig
./test
```



## 用例

```c++

#include<iostream>

#include "threadpool.h"
using namespace std;
int sum1(int a, int b) {
	return a + b;
}
int main() {
	ThreadPool pool;
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(2);

	future<int> r1 = pool.submitTask(sum1, 1, 1000);
	future<int> r2 = pool.submitTask(sum1, 1001, 2000);
	future<int> r3 = pool.submitTask([](int s, int e) {
		int sum = 0;
		for (int i = s; i <= e; i++) {
			sum += i;
		}
		return sum;
		}, 1, 1000);
	future<int> r4 = pool.submitTask(sum1, 2001, 3000);
	future<int> r5 = pool.submitTask(sum1, 3001, 4000);
	cout << r1.get()<<endl;
	cout << r2.get() << endl;
	cout << r3.get() << endl;
	cout << r4.get() << endl;
	cout << r5.get() << endl;
}
```



## 参考

施磊