#pragma once
#include <functional>
#include <utility>

namespace MyUtils::Memory {
    template <typename T>
    class MPSCQueue {
    private:
        struct Node {
            T data;
            atomic<Node*> next;

            Node() : next(nullptr) {}
            Node(T&& val) : data(move(val)), next(nullptr) {}
            Node(const T& val) : data(val), next(nullptr) {}
        };

    public:
        MPSCQueue() {
            Node* stub = new Node();
            _head.store(stub, memory_order_relaxed);
            _tail = stub;
        }

        ~MPSCQueue() {
            T temp;
            while (Dequeue(temp));
            delete _tail;
        }

        void Enqueue(T item) {
            Node* newNode = new Node(move(item));
            Node* prevHead = _head.exchange(newNode, memory_order_acq_rel);

            prevHead->next.store(newNode, memory_order_release);
        }

        bool Dequeue(T& outItem) {
            Node* tail = _tail;
            Node* next = tail->next.load(memory_order_acquire);

            if (next) {
                outItem = move(next->data);
                delete tail;

                _tail = next;
                return true;
            }

            return false;
        }

        bool IsEmpty() const {
            return _tail->next.load(memory_order_relaxed) == nullptr;
        }

    private:
        alignas(64) atomic<Node*> _head;
        alignas(64) Node* _tail;
    };

    template <typename T>
    class ObjectPool {
    public:
        ObjectPool(size_t chunkSize = 100) : _chunkSize(chunkSize) {
            Expand();
        }

        ~ObjectPool() {
            for (T* chunk : _chunks) {
                ::operator delete(chunk);
            }
        }

        template <typename... Args>
        shared_ptr<T> Acquire(Args&&... args) {
            unique_lock<mutex> lock(_lock);

            if (_freeList.empty()) {
                Expand();
            }

            T* ptr = _freeList.back();
            _freeList.pop_back();

            lock.unlock();

            new(ptr) T(forward<Args>(args)...);

            return shared_ptr<T>(ptr, [this](T* p) {
                p->~T();
                this->Return(p);
            });
        }

        // 현재 가용 개수
        size_t Size() {
            lock_guard<mutex> lock(_lock);
            return _freeList.size();
        }

    private:
        void Return(T* ptr) {
            lock_guard<mutex> lock(_lock);
            _freeList.push_back(ptr);
        }

        void Expand() {
            size_t size = sizeof(T) * _chunkSize;
			T* newChunk = static_cast<T*>(::operator new(size)); // 생성자 호출 없이 메모리만 chunkSize만큼 할당

            _chunks.push_back(newChunk);

            for (size_t i = 0; i < _chunkSize; ++i) {
                _freeList.push_back(newChunk + i); // 포인터 연산
            }
        }

    private:
        vector<T*> _freeList;
        vector<T*> _chunks;
        size_t _chunkSize;
        mutex _lock;
    };
}