#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include "thread_safe_queue.h"
#include "message.h"

// A single node in the mini3 overlay, with four worker threads.
class Server {
public:
    // node_id: unique name/IP for this instance
    // neighbors: list of gRPC endpoints to gossip to
    Server(std::string node_id,
           std::vector<std::string> neighbors);

    // Start all threads and block until shutdown
    void run();

    // Signal threads to stop (not used yet)
    void shutdown();

private:
    // Four concurrent loops, each on its own thread:
    void listenerLoop();   // receive gRPC → enqueue Message
    void processorLoop();  // dequeue Message → dispatch
    void heartbeatLoop();  // every 500 ms → send Heartbeat
    void routingLoop();    // every 500 ms → rebuild routing table

    std::string                  node_id_;
    std::vector<std::string>     neighbors_;
    ThreadSafeQueue<Message>     inbound_;      // shared queue for all incoming msgs
    std::atomic<bool>            running_{false};

    std::thread                  t_listener_;
    std::thread                  t_processor_;
    std::thread                  t_heartbeat_;
    std::thread                  t_routing_;

    // TODO: add peerInfo map, routing table, gRPC stubs, metrics storage, etc.
};

#endif // SERVER_H
