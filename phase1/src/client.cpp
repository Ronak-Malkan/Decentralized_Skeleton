#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "mini3.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using mini3::Mini3Service;
using mini3::TaskRequest;
using mini3::TaskResponse;

int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "Usage: client <server_addr> <task_id> <payload> <reply_to>\n";
    return 1;
  }
  std::string server_addr = argv[1];
  std::string task_id     = argv[2];
  std::string payload     = argv[3];
  std::string reply_to    = argv[4];

  // Create the stub
  auto channel = grpc::CreateChannel(server_addr, grpc::InsecureChannelCredentials());
  auto stub    = Mini3Service::NewStub(channel);

  // Build the request
  TaskRequest req;
  req.set_task_id(task_id);
  req.set_payload(payload);
  req.set_reply_to(reply_to);

  // RPC
  ClientContext ctx;
  TaskResponse resp;
  Status status = stub->SubmitTask(&ctx, req, &resp);

  if (!status.ok()) {
    std::cerr << "RPC failed: " << status.error_message() << "\n";
    return 1;
  }

  // Print the result
  std::cout << "Task " << resp.task_id() << " got response: "
            << resp.result() << std::endl;
  return 0;
}
