// AcePS kernel-services implementation: contains non-blocking primitives suitable for early guest stubs.
#include "engine/kernel/kernel_services.h"
namespace AcePS::Kernel { void EventQueue::push(std::uint64_t e){events_.push(e);} bool EventQueue::tryPop(std::uint64_t&e){if(events_.empty())return false;e=events_.front();events_.pop();return true;} bool Semaphore::tryWait(){if(!count_)return false;--count_;return true;} void Semaphore::signal(){++count_;} }
