# modernThreadPool
modern effective C++ thread pool

## modernthreadpool是基于C++11的轻量级高效线程池

具体的说，有一些线程专用于线程池和作业容器，作业动态地进入池，当存在空闲线程时，线程池从容器中获取并删除作业，然后在该线程上运行作业。

当您希望最大限度地减少加载和销毁线程的时间，以及希望限制并行运行的并行作业的数量时，线程池很有帮助，例如，可以在线程池中处理耗时的事件处理程序，以使UI更具响应性。

## 特点：
*符合C++11标准，经测试可以在Clion2024.1.1上编译，使用的编译工具为gcc，mingw32
*one header-only 只有一个头文件，无需编译二进制库
*支持查询空闲线程数并动态调整线程池大小
*只需一个API，即可将任何可整理对象推送到线程池：对象、函数、函数、绑定表达式的结果等，支持可变参数与lambda
*使用标准c++ futures获取任何类型的返回值
*使用标准c++ futures获得被捕获的异常

## 使用示例
'''
void first(int id) {
    std::cout << "hello from 线程 " << id << "\n";
}

void aga(int id, int par) {
    std::cout << "hello from 线程 " << id << ", 方法的参数为 " << par <<'\n';
}

struct Third {
    Third(int v) { this->v = v; std::cout << "Third类构造函数运行 " << this->v << '\n'; }
    Third(Third && c) { this->v = c.v; std::cout<<"Third类移动构造函数运行\n"; }
    Third(const Third & c) { this->v = c.v; std::cout<<"Third拷贝构造函数运行\n"; }
    ~Third() { std::cout << "Third类析构函数运行\n"; }
    int v;
};

class thread_pool;

void mmm(int id, const std::string & s) {
    std::cout << "mmm function 被线程" << id <<"运行，接受一个参数为：" << s << '\n';
}

void ugu(int id, Third & t) {
    //std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    std::cout << "hello from线程" << id << ",接受一个参数为third类" << t.v <<'\n';
}

int main() {
    mtp::thread_pool p(10);//当前设置了10个线程在线程池内
    //往任务队列中加入方法
    std::future<void> qw = p.push(std::ref(first));
    p.push(first);
    p.push(aga, 7);

    {
        //代码块内实现局部类
        struct Second {
            Second(const std::string & s) { std::cout << "Second类构造函数运行\n"; this->s = s; }
            Second(Second && c) { std::cout << "Second类移动构造函数运行\n"; s = std::move(c.s); }
            Second(const Second & c) { std::cout << "Second类拷贝构造函数运行\n"; this->s = c.s; };
            ~Second() { std::cout << "Second类析构函数运行\n"; }
            void operator()(int id) const {
                std::cout << "hello from 线程" << id << ' ' << this->s << '\n';
            }
        private:
            std::string s;
        } second(", functor");

        p.push(std::ref(second));
        p.push(const_cast<const Second &>(second));
        p.push(std::move(second));
        p.push(second);
        p.push(Second(", functor"));
    }
    {
        Third t(100);

        p.push(ugu, std::ref(t));
        p.push(ugu, t);
        p.push(ugu, std::move(t));

    }
    p.push(ugu, Third(200));



    std::string s = ", lambda";
    //lambda表达式也可以加入任务队列
    p.push([s](int id){
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        std::cout << "hello from 线程" << id << ' ' << s << '\n';
    });

    p.push([s](int id){  // lambda
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        std::cout << "hello from 线程" << id << ' ' << s << '\n';
    });

    p.push(mmm, "worked");

    auto f = p.pop();
    if (f) {
        std::cout << "pop方法运行 ";
        f(0);
    }
    //重设线程的数量
    p.resize(1);

    std::string s2 = "result";
    auto f1 = p.push([s2](int){
        return s2;
    });

    std::cout << "returned " << f1.get() << '\n';

    auto f2 = p.push([](int){
        throw std::exception();
    });

    try {
        f2.get();
    }
    catch (std::exception & e) {
        std::cout << "捕获到了异常\n";
    }

    //获取线程id为0的线程
    auto & th = p.get_thread(0);

    return 0;
}

