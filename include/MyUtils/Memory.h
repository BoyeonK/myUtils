#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <utility>
#include <new>

namespace MyUtils::Memory {
    template <typename T>
    class ObjectPool {
    public:
        static constexpr int BATCH_SIZE = 500;
        static constexpr int MAX_LOCAL_SIZE = 2000;

        // (순수 정적 클래스 의도)
        ObjectPool() = delete;
        ~ObjectPool() = delete;

        template <typename... Args>
        static std::shared_ptr<T> Acquire(Args&&... args) {
            T* ptr = Pop();
            new(ptr) T(std::forward<Args>(args)...);

            return std::shared_ptr<T>(ptr, [](T* p) {
                p->~T();
                Push(p);
            });
        }

        template <typename... Args>
        static T* AcquireRaw(Args&&... args) {
            T* ptr = Pop();
            new(ptr) T(std::forward<Args>(args)...);
            return ptr;
        }

        static void ReturnRaw(T* ptr) {
            Push(ptr);
        }

    private:
        static T* Pop() {
            std::vector<T*>& local = GetLocalPool();

            if (local.empty()) {
                RefillLocalPool(local);
            }

            if (local.empty()) {
                void* ptr = ::operator new(sizeof(T));
                return static_cast<T*>(ptr);
            }

            T* ptr = local.back();
            local.pop_back();
            return ptr;
        }

        static void Push(T* ptr) {
            std::vector<T*>& local = GetLocalPool();
            local.push_back(ptr);

            if (local.size() > MAX_LOCAL_SIZE) {
                FlushLocalPool(local);
            }
        }

        static std::vector<T*>& GetGlobalPool() {
            static std::vector<T*> _globalPool;
            return _globalPool;
        }

        static std::mutex& GetGlobalLock() {
            static std::mutex _lock;
            return _lock;
        }

        static std::vector<T*>& GetLocalPool() {
            thread_local std::vector<T*> _localPool;
            if (_localPool.capacity() < MAX_LOCAL_SIZE) {
                _localPool.reserve(MAX_LOCAL_SIZE);
            }
            return _localPool;
        }

        static void RefillLocalPool(std::vector<T*>& local) {
            std::lock_guard<std::mutex> lock(GetGlobalLock());
            std::vector<T*>& global = GetGlobalPool();
            int count = 0;
            while (!global.empty() && count < BATCH_SIZE) {
                local.push_back(global.back());
                global.pop_back();
                count++;
            }
        }

        static void FlushLocalPool(std::vector<T*>& local) {
            std::lock_guard<std::mutex> lock(GetGlobalLock());
            std::vector<T*>& global = GetGlobalPool();
            int count = 0;
            while (local.size() > (MAX_LOCAL_SIZE - BATCH_SIZE) && count < BATCH_SIZE) {
                global.push_back(local.back());
                local.pop_back();
                count++;
            }
        }
    };

    template <typename T>
    class MPSCQueue {
    public:
        struct Node {
            T data;
            std::atomic<Node*> next;

            Node() : next(nullptr) {}
            Node(T&& val) : data(std::move(val)), next(nullptr) {}
            Node(const T& val) : data(val), next(nullptr) {}
        };
        using NodePool = ObjectPool<Node>;

        MPSCQueue() {
            Node* stub = NodePool::AcquireRaw();
            _head.store(stub, std::memory_order_relaxed);
            _tail = stub;
        }

        ~MPSCQueue() {
            T temp;
            while (Dequeue(temp));

            _tail->~Node();
            NodePool::ReturnRaw(_tail);
        }

        void Enqueue(T item) {
            Node* newNode = NodePool::AcquireRaw(std::move(item));

            Node* prevHead = _head.exchange(newNode, std::memory_order_acq_rel);
            prevHead->next.store(newNode, std::memory_order_release);
        }

        bool Dequeue(T& outItem) {
            Node* tail = _tail;
            Node* next = tail->next.load(std::memory_order_acquire);

            if (next) {
                outItem = std::move(next->data);
                tail->~Node();
                NodePool::ReturnRaw(tail);

                _tail = next;
                return true;
            }

            return false;
        }

        bool IsEmpty() const {
            return _tail->next.load(std::memory_order_relaxed) == nullptr;
        }

    private:
        alignas(64) std::atomic<Node*> _head;
        alignas(64) Node* _tail;
    };
}