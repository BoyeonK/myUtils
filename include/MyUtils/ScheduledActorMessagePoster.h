#pragma once
#include "Actor.h"
#include <queue>

using namespace std;

namespace MyUtils {
    struct ScheduledMessage {
        ScheduledMessage(weak_ptr<Actor> owner, shared_ptr<Message> message)
            : _ownerWRef(owner), _actorMessage(message) {
        }

        weak_ptr<Actor> _ownerWRef;
        shared_ptr<Message> _actorMessage;
    };

    struct TimerMessage {
        bool operator<(const TimerMessage& other) const {
            return executeTick > other.executeTick;
        }

        uint64_t executeTick = 0;
        shared_ptr<ScheduledMessage> messageRef;
    };

    class ActorEventScheduler {
    public:
        void Reserve(uint64_t tickAfter, weak_ptr<Actor> owner, shared_ptr<Message> message);
        void AddAndDistribute(uint64_t now);
        void Clear();

    private:
        priority_queue<TimerMessage> _orderedMessages;
        mutex _addLock;
        vector<TimerMessage> _toBeAddedMessages;
    };
}
