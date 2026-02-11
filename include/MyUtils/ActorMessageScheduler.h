#pragma once

#include <queue>
#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>

namespace MyUtils {
    class Actor;
    struct Message;

    struct ScheduledMessage {
        ScheduledMessage(std::weak_ptr<Actor> owner, std::shared_ptr<Message> message)
            : _ownerWRef(owner), _actorMessage(message) {
        }

        std::weak_ptr<Actor> _ownerWRef;
        std::shared_ptr<Message> _actorMessage;
    };

    struct TimerMessage {
        bool operator<(const TimerMessage& other) const {
            return executeTick > other.executeTick;
        }

        uint64_t executeTick = 0;
        std::shared_ptr<ScheduledMessage> messageRef;
    };

    class ActorMessageScheduler {
    public:
        void Reserve(uint64_t tickAfter, std::weak_ptr<Actor> owner, std::shared_ptr<Message> message);
        void AddAndDistribute(uint64_t now);
        void Clear();

    private:
        std::priority_queue<TimerMessage> _orderedMessages;
        std::mutex _addLock;
        std::vector<TimerMessage> _toBeAddedMessages;
    };
}
