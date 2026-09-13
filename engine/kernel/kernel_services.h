// AcePS kernel-services declaration: offers deterministic guest event queues and semaphore primitives.
#pragma once
#include <cstdint>
#include <queue>
namespace AcePS::Kernel { class EventQueue { public: void push(std::uint64_t event); bool tryPop(std::uint64_t& event); private: std::queue<std::uint64_t> events_; }; class Semaphore { public: explicit Semaphore(unsigned initial=0):count_(initial){} bool tryWait(); void signal(); private: unsigned count_; }; }
