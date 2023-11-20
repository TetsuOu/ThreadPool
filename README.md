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





## 参考

施磊