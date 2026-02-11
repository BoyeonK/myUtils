#include "MyUtils/Actor.h"
#include "MyUtils/Memory.h"
#include "MyUtils/GlobalVariables.h"

namespace MyUtils {
	void Actor::Push(shared_ptr<Message> message) {
		const int32_t prevCount = _messageCount.fetch_add(1);
		_messages.Enqueue(message);
		GActorQueue->enqueue(shared_from_this());
	}

	void Actor::ProcessMyMessageBox() {
		MyUtils::LCurrentActor = shared_from_this();
		/*
		_isExecuting.store(true);
		shared_ptr<Message> message;
		while (_messages.Dequeue(message)) {
			_messageCount.fetch_sub(1);
			if (message) {
				message->Process();
			}
		}
		_isExecuting.store(false);
		LCurrentActor = nullptr;
		*/
	}
}
