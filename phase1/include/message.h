#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>
#include "mini3.pb.h"
#include "mini3.grpc.pb.h"

enum class MessageType {
    HEARTBEAT,
    TASK_REQUEST
};

// A generic wrapper for incoming gRPC calls.
// The processor thread inspects `type` to know which field is valid.
struct Message {
    MessageType           type;
    std::string           from;           // node ID of sender
    mini3::Heartbeat      heartbeat;      // valid if type==HEARTBEAT
    mini3::TaskRequest    task_request;   // valid if type==TASK_REQUEST
};

#endif // MESSAGE_H
