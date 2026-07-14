#ifndef __ring_buffer__
#define __ring_buffer__

#define BUFFER_SIZE 8 /*kfifo的大小，必须是2的幂次方*/

/* 定义环形缓冲区的结构体 */
/* 头上读,尾巴写 */
struct ring_buffer {
  unsigned int buf[BUFFER_SIZE];
  int head; // 读指针（指向下一个要读的位置）
  int tail; // 写指针（指向下一个要写的位置）
};

/* 环形缓冲区初始化 */
void ring_buffer_init(struct ring_buffer *rb);
/* 环形缓冲区判空 */
int rb_is_empty(struct ring_buffer *rb);
/* 环形缓冲区判满 */
int rb_is_full(struct ring_buffer *rb);
/* 写数据 */
int rb_push(struct ring_buffer *rb, unsigned int val);
/* 读数据 */
int rb_pop(struct ring_buffer *rb, unsigned int *val);
/* 获取当前有效数据的个数 */
int rb_count(struct ring_buffer *rb);

#endif // !__ring_buffer__
