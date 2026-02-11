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
        static ObjectPool& Instance() {
            static ObjectPool* instance = new ObjectPool();
            return *instance;
        }

        template <typename... Args>
        static std::shared_ptr<T> Acquire(Args&&... args) {
            T* ptr = Instance().AcquireRawImpl(std::forward<Args>(args)...);

            return std::shared_ptr<T>(ptr, [](T* p) {
                p->~T();
                Instance().ReturnRawImpl(p);
            });
        }

        template <typename... Args>
        static T* AcquireRaw(Args&&... args) {
            return Instance().AcquireRawImpl(std::forward<Args>(args)...);
        }

        static void ReturnRaw(T* ptr) {
            Instance().ReturnRawImpl(ptr);
        }

    private:
        ObjectPool(size_t chunkSize = 100) : _chunkSize(chunkSize) { Expand(); }
        ~ObjectPool() {
            for (void* chunk : _chunks) {
                ::operator delete(chunk);
            }
        }

        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        template <typename... Args>
        T* AcquireRawImpl(Args&&... args) {
            std::unique_lock<std::mutex> lock(_lock);
            if (_freeList.empty()) Expand();

            T* ptr = _freeList.back();
            _freeList.pop_back();
            lock.unlock();

            new(ptr) T(std::forward<Args>(args)...);
            return ptr;
        }

        void ReturnRawImpl(T* ptr) {
            std::lock_guard<std::mutex> lock(_lock);
            _freeList.push_back(ptr);
        }

        void Expand() {
            size_t size = sizeof(T) * _chunkSize;
            void* newChunkPtr = ::operator new(size);
            T* newChunk = static_cast<T*>(newChunkPtr);

            _chunks.push_back(newChunk);
            for (size_t i = 0; i < _chunkSize; ++i)
                _freeList.push_back(newChunk + i);
        }

        std::vector<T*> _freeList;
        std::vector<void*> _chunks;
        size_t _chunkSize;
        std::mutex _lock;
    };

    template <typename T>
    class MPSCQueue {
    private:
        struct Node {
            T data;
            std::atomic<Node*> next;

            Node() : next(nullptr) {}
            Node(T&& val) : data(std::move(val)), next(nullptr) {}
            Node(const T& val) : data(val), next(nullptr) {}
        };

        // Node 전용 풀 정의
        using NodePool = ObjectPool<Node>;

    public:
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