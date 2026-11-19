#pragma once
// Only the queue operations used by keyboard_input.cpp are supported.
// Single browser thread: waits are nonblocking; platform::pump_events yields.
#include <cstring>
#include <deque>
#include <vector>
struct BrowserQueue { size_t capacity, item_size; std::deque<std::vector<unsigned char>> items; };
using QueueHandle_t = BrowserQueue*;
inline QueueHandle_t xQueueCreate(size_t count, size_t bytes) { return new BrowserQueue{count, bytes, {}}; }
inline void xQueueReset(QueueHandle_t queue) { queue->items.clear(); }
inline unsigned uxQueueMessagesWaiting(QueueHandle_t queue) { return unsigned(queue->items.size()); }
inline int xQueueSend(QueueHandle_t queue, const void* item, unsigned) {
    if (queue->items.size() == queue->capacity) return 0;
    const auto* bytes = static_cast<const unsigned char*>(item);
    queue->items.emplace_back(bytes, bytes + queue->item_size);
    return 1;
}
inline int xQueueReceive(QueueHandle_t queue, void* item, unsigned) {
    if (queue->items.empty()) return 0;
    std::memcpy(item, queue->items.front().data(), queue->item_size);
    queue->items.pop_front();
    return 1;
}
