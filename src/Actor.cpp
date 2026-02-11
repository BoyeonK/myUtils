#include "MyUtils/Actor.h"
#include "MyUtils/Memory.h"
#include "MyUtils/GlobalVariables.h"

namespace MyUtils {
	void Actor::Push(shared_ptr<Message> message) {
		const int32_t prevCount = _messageCount.fetch_add(1);
		_messages.Enqueue(message);
		bool expected = false;
		if (_isExecuting.compare_exchange_strong(expected, true))
			GActorQueue->enqueue(shared_from_this());
	}

	void Actor::ProcessMyMessageBox() {
		MyUtils::LCurrentActor = shared_from_this();

        while (true) {
            shared_ptr<Message> msg;
            while (_messages.Dequeue(msg)) {
                if (msg)
                    msg->Process();
            }
            _isExecuting.store(false);
            if (!_messages.IsEmpty()) {
                bool expected = false;
                if (_isExecuting.compare_exchange_strong(expected, true))
                    continue;
            }
            break;
        }

        MyUtils::LCurrentActor = nullptr;
	}
}
