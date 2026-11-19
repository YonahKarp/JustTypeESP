#pragma once
#include "FreeRTOS.h"
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>
struct TestQueue{std::mutex mutex;std::condition_variable cv;size_t capacity,item_size;std::deque<std::vector<char>> items;};
using QueueHandle_t=TestQueue*;
inline QueueHandle_t xQueueCreate(size_t n,size_t size){auto* q=new TestQueue;q->capacity=n;q->item_size=size;return q;}
inline BaseType_t xQueueSend(QueueHandle_t q,const void* item,TickType_t wait){std::unique_lock<std::mutex> lock(q->mutex);if(wait==0&&q->items.size()==q->capacity)return pdFALSE;q->cv.wait(lock,[&]{return q->items.size()<q->capacity;});q->items.emplace_back(static_cast<const char*>(item),static_cast<const char*>(item)+q->item_size);q->cv.notify_all();return pdTRUE;}
inline BaseType_t xQueueReceive(QueueHandle_t q,void* item,TickType_t wait){std::unique_lock<std::mutex> lock(q->mutex);if(wait==0&&q->items.empty())return pdFALSE;q->cv.wait(lock,[&]{return !q->items.empty();});std::memcpy(item,q->items.front().data(),q->item_size);q->items.pop_front();q->cv.notify_all();return pdTRUE;}
