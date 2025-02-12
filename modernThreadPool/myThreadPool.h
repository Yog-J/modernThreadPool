//
// Created by Lenovo on 2025/1/14.
//

#ifndef MODERNTHREADPOOL_MYTHREADPOOL_H
#define MODERNTHREADPOOL_MYTHREADPOOL_H

#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include "vector"
#include "memory"
#include "thread"

namespace mtp{

    namespace myQueue{
        template <typename T>
        class Queue{
        public:
            bool push(T const& value)
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->q.push(std::move(value));
                return true;
            }

            bool pop(T& v)
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                if(this->q.empty()) return false;//队列为空无法弹出任务
                v = this->q.front();//记录弹出的任务
                this->q.pop();
                return true;
            }

            bool empty()
            {
                std::unique_lock<std::mutex> lock(this->mutex);//判断队列是否为空
                return this->q.empty();
            }
        private:
            std::queue<T> q;
            std::mutex mutex;
        };
    }

    class thread_pool{
    public:

        thread_pool(){
            this->init();
        }

        thread_pool(int nThreads)
        {
            this->init();
            this->resize(nThreads);
        }

        ~thread_pool()
        {
            this->stop(true);
        }

        //获取运行的线程数量
        int size()
        {
            return this->threads.size();
        }

        //获取正在等待任务的线程
        int num_waiting_threads() { return this->waiting_threads; }

        //获取线程id
        std::thread & get_thread(int i) { return *this->threads[i]; }

        void resize(int nThreads)
        {
            if(!this->isStop && !this->isDone)//线程停止或者任务完成都无法更改线程池的线程数
            {
                int oldNThreads = this->threads.size();//记录之前的线程数
                if(oldNThreads <= nThreads)//如果线程变多
                {
                    this->threads.resize(nThreads);
                    this->flags.resize(nThreads);

                    for (int i = oldNThreads; i < nThreads; ++i) {
                        this->flags[i] = std::make_shared<std::atomic<bool>>(false);//先将线程id对应的状态标志置为false
                        this->set_thread(i);//开辟新的线程
                    }
                }
                else //如果线程变少了
                {
                    for (int i = oldNThreads - 1; i >= nThreads; --i) {
                        *this->flags[i] = true;
                        this->threads[i]->detach();//多余的线程结束
                    }
                    {

                        std::unique_lock<std::mutex> lock(this->mutex);
                        this->cv.notify_all();
                    }
                    this->threads.resize(nThreads);  //更新线程池中现存的线程
                    this->flags.resize(nThreads);//更新线程状态
                }
            }
        }

        //清空任务队列
        void clear_queue() {
            std::function<void(int id)> * _f;
            while (this->q.pop(_f))
                delete _f;//清空任务队列
        }

        //从任务队列中弹出一个任务
        std::function<void(int)> pop() {
            std::function<void(int id)> * _f = nullptr;
            this->q.pop(_f);
            std::unique_ptr<std::function<void(int id)>> func(_f);
            std::function<void(int)> f;
            if (_f)
                f = *_f;
            return f;
        }

        //默认等待正在运行的线程运行完任务再停止所有的线程
        //如果isWait为true，则队列中的所有函数都将运行，否则将在不运行函数的情况下清除队列
        void stop(bool isWait = false)
        {
            if (!isWait)
            {
                if (this->isStop)
                    return;
                this->isStop = true;
                for (int i = 0, n = this->size(); i < n; ++i) {
                    *this->flags[i] = true;//将所有线程状态设置为停止
                }
                this->clear_queue();//清空任务队列
            }
            else {
                if (this->isDone || this->isStop)
                    return;
                this->isDone = true;//结束闲置的进程
            }
            {
                std::unique_lock<std::mutex> lock(this->mutex);
                this->cv.notify_all();//停止所有线程
            }
            for (int i = 0; i < static_cast<int>(this->threads.size()); ++i) {  //等待没有运行完任务的线程运行
                if (this->threads[i]->joinable())
                    this->threads[i]->join();
            }
            // 如果线程池中已经没有了线程运行，但是任务队列里还有任务，这些任务不会被线程删除，因此要清空这些任务
            this->clear_queue();
            this->threads.clear();
            this->flags.clear();
        }

        //任务队列加入新任务
        //返回一个 std::future，以便获取f执行的结果
        template<typename F, typename... Rest>
        auto push(F && f, Rest&&... rest) ->std::future<decltype(f(0, rest...))> {
            auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(
                    std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...)
            );
            auto _f = new std::function<void(int id)>([pck](int id) {
                (*pck)(id);
            });
            this->q.push(_f);
            std::unique_lock<std::mutex> lock(this->mutex);
            this->cv.notify_one();
            return pck->get_future();
        }

        template<typename F>
        auto push(F && f) ->std::future<decltype(f(0))> {
            auto pck = std::make_shared<std::packaged_task<decltype(f(0))(int)>>(std::forward<F>(f));
            auto _f = new std::function<void(int id)>([pck](int id) {
                (*pck)(id);
            });
            this->q.push(_f);
            std::unique_lock<std::mutex> lock(this->mutex);
            this->cv.notify_one();
            return pck->get_future();
        }

    private:
        thread_pool(const thread_pool&) = delete;
        thread_pool(thread_pool&&) = delete;
        thread_pool& operator= (const thread_pool&) = delete;
        thread_pool& operator= (const thread_pool&&) = delete;

        void init()
        {
            this->waiting_threads = 0;
            this->isDone = false;
            this->isStop = false;
        }

        void set_thread(int i) {
            std::shared_ptr<std::atomic<bool>> flag(this->flags[i]);//记录该线程当前的状态
            auto f = [this, i, flag]() {
                std::atomic<bool> & _flag = *flag;
                std::function<void(int id)> * _f;
                bool isPop = this->q.pop(_f);//判断队列是否为空
                while (true) {
                    while (isPop) {
                        std::unique_ptr<std::function<void(int id)>> func(_f);
                        (*_f)(i);
                        if (_flag)
                            return;
                        else
                            isPop = this->q.pop(_f);
                    }
                    //任务队列已经为空
                    std::unique_lock<std::mutex> lock(this->mutex);
                    ++this->waiting_threads;
                    this->cv.wait(lock, [this, &_f, &isPop, &_flag](){ isPop = this->q.pop(_f); return isPop || this->isDone || _flag; });
                    --this->waiting_threads;
                    if (!isPop)//任务队列为空直接返回
                        return;
                }
            };
            this->threads[i].reset(new std::thread(f));
        }

        std::vector<std::unique_ptr<std::thread>> threads;//开辟的线程存储在threads里面
        std::vector<std::shared_ptr<std::atomic<bool>>> flags;
        std::atomic<int> waiting_threads;//闲置的线程数量
        myQueue::Queue<std::function<void(int id)> *> q;
        std::atomic<bool> isDone;//队列里的任务完成没有
        std::atomic<bool> isStop;//线程池是否停止
        std::mutex mutex;
        std::condition_variable cv;
    };
}
#endif //MODERNTHREADPOOL_MYTHREADPOOL_H
