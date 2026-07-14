#include "ring_buffer.h"
/* 环形缓冲区初始化 */
void ring_buffer_init(struct ring_buffer *rb) {
  rb->head = 0;
  rb->tail = 0;
}
/* 环形缓冲区判空 */
int rb_is_empty(struct ring_buffer *rb) {
  /* 判断首和尾巴是否相等 */
  return rb->head == rb->tail;
}
/* 环形缓冲区判满 */
int rb_is_full(struct ring_buffer *rb) {
  /* 预留一个元素位置，避免“空”和“满”条件冲突 */
  return ((rb->tail + 1) % BUFFER_SIZE) == rb->head;
}
/* 写数据 */
int rb_push(struct ring_buffer *rb, unsigned int val) {
  if (rb_is_full(rb)) {
    return -1; // 缓冲区已满，写入失败
  }
  rb->buf[rb->tail] = val;
  rb->tail = (rb->tail + 1) % BUFFER_SIZE; // 回绕
  return 0;
}
/* 读数据 */
int rb_pop(struct ring_buffer *rb, unsigned int *val) {
  if (rb_is_empty(rb)) {
    return -1; // 缓冲区为空，读取失败
  }
  *val = rb->buf[rb->head];
  rb->head = (rb->head + 1) % BUFFER_SIZE;
  return 0;
}
/* 获取当前有效数据的个数 */
int rb_count(struct ring_buffer *rb) {
  if (rb->tail >= rb->head) { /* 指针未跨越 */
    return rb->tail - rb->head;
  } else { /* 指针跨越 */
    return BUFFER_SIZE - (rb->head - rb->tail);
  }
}