#pragma once
#include <atomic>
#include <memory>
#include <tuple>
#include <utility>
#include <functional>
#include "MyUtils/Memory.h"

using namespace std;

namespace MyUtils {
	struct Message {
		Message() {}
		virtual ~Message() {}

		virtual void Process() = 0;
	};

	struct Task : public Message {
	public:
		Task(function<void()>&& callback) : _func(move(callback)) {}

		template<typename T, typename... Args>
		Task(weak_ptr<T> ownerWRef, void(T::* memFunc)(Args...), Args&&... args) {
			auto argsTuple = make_tuple(forward<Args>(args)...);
			_func = [ownerWRef, memFunc, tup = move(argsTuple)]() mutable {
				shared_ptr<T> owner = ownerWRef.lock();
				if (owner != nullptr)
					apply([&](auto&&... unpacked) {
					(owner.get()->*memFunc)(forward<decltype(unpacked)>(unpacked)...);
						}, move(tup));
				};
		}

		virtual void Process() override {
			if (_func) {
				_func();
				_func = nullptr;
			}
		}


	private:
		function<void()> _func;
	};

	class Actor : public enable_shared_from_this<Actor> {
	public:
		virtual ~Actor() = default;

		void PostTask(function<void()>&& callback) {
			Push(MyUtils::Memory::ObjectPool<Task>::Acquire(std::move(callback)));
		}

		template<typename T, typename... Args>
		void PostTask(void(T::* memFunc)(Args...), Args&&... args) {
			weak_ptr<T> ownerWRef = static_pointer_cast<T>(shared_from_this());
			Push(MyUtils::Memory::ObjectPool<Task>::Acquire(ownerWRef, memFunc, forward<Args>(args)...));
		}

		/*
		void PostTaskAfter(uint64_t tickAfter, function<void()>&& callback) {
			shared_ptr<Task> taskRef = MyUtils::Memory::ObjectPool<Task>::Acquire(std::move(callback));
			GActorEventScheduler->Reserve(tickAfter, shared_from_this(), taskRef);
		}
		*/

		/*
		template<typename T, typename... Args>
		void PostTaskAfter(uint64_t tickAfter, void(T::* memFunc)(Args...), Args&&... args) {
			weak_ptr<T> ownerWRef = static_pointer_cast<T>(shared_from_this());
			shared_ptr<Task> taskRef = MyUtils::Memory::ObjectPool<Task>::Acquire(ownerWRef, memFunc, forward<Args>(args)...);
			GActorEventScheduler->Reserve(tickAfter, shared_from_this(), taskRef);
		}
		*/

		void ProcessMyMessageBox();

	private:
		void Push(shared_ptr<Message> message);

	protected:
		MyUtils::Memory::MPSCQueue<shared_ptr<Message>> _messages;
		atomic<int32_t> _messageCount = 0;
		atomic<bool> _isExecuting = false;
	};
}