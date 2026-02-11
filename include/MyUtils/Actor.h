#pragma once
#include <atomic>
#include <memory>
#include <tuple>
#include <utility>
#include <functional>
#include "MyUtils/Memory.h"

namespace MyUtils {
	struct Message {
		Message() {}
		virtual ~Message() {}

		virtual void Process() = 0;
	};

	struct Task : public Message {
	public:
		Task(std::function<void()>&& callback) : _func(std::move(callback)) {}

		template<typename T, typename... Args>
		Task(std::weak_ptr<T> ownerWRef, void(T::* memFunc)(Args...), Args&&... args) {
			auto argsTuple = std::make_tuple(std::forward<Args>(args)...);
			_func = [ownerWRef, memFunc, tup = std::move(argsTuple)]() mutable {
				std::shared_ptr<T> owner = ownerWRef.lock();
				if (owner != nullptr)
					std::apply([&](auto&&... unpacked) {
					(owner.get()->*memFunc)(std::forward<decltype(unpacked)>(unpacked)...);
						}, std::move(tup));
				};
		}

		virtual void Process() override {
			if (_func) {
				_func();
				_func = nullptr;
			}
		}


	private:
		std::function<void()> _func;
	};

	class Actor : public std::enable_shared_from_this<Actor> {
	public:
		virtual ~Actor() = default;

		void PostTask(std::function<void()>&& callback) {
			Push(MyUtils::Memory::ObjectPool<Task>::Acquire(std::move(callback)));
		}

		template<typename T, typename... Args>
		void PostTask(void(T::* memFunc)(Args...), Args&&... args) {
			std::weak_ptr<T> ownerWRef = std::static_pointer_cast<T>(shared_from_this());
			Push(MyUtils::Memory::ObjectPool<Task>::Acquire(ownerWRef, memFunc, std::forward<Args>(args)...));
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
		void Push(std::shared_ptr<Message> message);

	protected:
		MyUtils::Memory::MPSCQueue<std::shared_ptr<Message>> _messages;
		std::atomic<int32_t> _messageCount = 0;
		std::atomic<bool> _isExecuting = false;
	};
}