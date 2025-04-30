#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include "mini3.pb.h"
#include "mini3.grpc.pb.h"

enum class MessageType {
    HEARTBEAT,
    TASK_REQUEST
};

// A generic wrapper that we push into our queue
struct Message {
    MessageType type;
    std::string from;                       // node ID of sender

    // Only one of these is valid, based on `type`
    mini3::Heartbeat   heartbeat;
    mini3::TaskRequest task_request;
};

#endif // MESSAGE_H
