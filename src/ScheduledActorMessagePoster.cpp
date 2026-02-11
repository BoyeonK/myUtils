#include "MyUtils/ScheduledActorMessagePoster.h"
#include <chrono>

namespace MyUtils {
    void ActorEventScheduler::Reserve(uint64_t tickAfter, weak_ptr<Actor> owner, shared_ptr<Message> message) {
        using namespace std::chrono;
        const uint64_t postTick = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() + tickAfter;

        shared_ptr<ScheduledMessage> msgRef = MyUtils::Memory::ObjectPool<ScheduledMessage>::Acquire(owner, message);
        lock_guard<mutex> lock(_addLock);
        _toBeAddedMessages.push_back(TimerMessage{ postTick, msgRef });
    }

    void ActorEventScheduler::AddAndDistribute(uint64_t now) {
        {
            lock_guard<mutex> lock(_addLock);
            for (const TimerMessage& timerMessage : _toBeAddedMessages) {
                _orderedMessages.push(timerMessage);
            }
            _toBeAddedMessages.clear();
        }

        vector<TimerMessage> onTimeMessages;
        while (!_orderedMessages.empty()) {
            const TimerMessage& timerMsg = _orderedMessages.top();
            if (now < timerMsg.executeTick)
                break;

            onTimeMessages.push_back(timerMsg);
            _orderedMessages.pop();
        }

        for (TimerMessage& tMsg : onTimeMessages) {
            if (auto owner = tMsg.messageRef->_ownerWRef.lock()) {
                owner->Push(tMsg.messageRef->_actorMessage);
            }
        }
    }

    void ActorEventScheduler::Clear() {
        {
            lock_guard<mutex> lock(_addLock);
            _toBeAddedMessages.clear();
        }

        _orderedMessages = priority_queue<TimerMessage>();
    }
}