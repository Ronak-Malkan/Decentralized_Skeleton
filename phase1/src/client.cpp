#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

#include <grpcpp/grpcpp.h>
#include "mini3.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using mini3::Mini3Service;
using mini3::TaskRequest;
using mini3::TaskResponse;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: client <task_id> <payload> <reply_to> <server1> [server2...]\n";
        return 1;
    }

    std::string task_id  = argv[1];
    std::string payload  = argv[2];
    std::string reply_to = argv[3];

    std::vector<std::string> servers;
    for (int i = 4; i < argc; ++i) {
        servers.push_back(argv[i]);
    }

    for (const auto& addr : servers) {
        auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        auto stub    = Mini3Service::NewStub(channel);

        TaskRequest req;
        req.set_task_id(task_id);
        req.set_payload(payload);
        req.set_reply_to(reply_to);

        TaskResponse resp;
        ClientContext ctx;
        ctx.set_deadline(
            std::chrono::system_clock::now() + std::chrono::seconds(5)
        );

        Status status = stub->SubmitTask(&ctx, req, &resp);
        if (status.ok()) {
            std::cout << "Task " << resp.task_id()
                      << " got response: " << resp.result() << "\n";
            return 0;
        } else {
            std::cerr << "RPC to " << addr
                      << " failed: " << status.error_message() << "\n";
        }
    }

    std::cerr << "All servers failed.\n";
    return 1;
}
