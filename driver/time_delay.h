#ifndef __TIME_DELAY_H__
#define __TIME_DELAY_H__

typedef void (*time_delay_func_t)(void);


void time_init(void);
uint64_t time_get_now(void);
uint64_t time_get_us(void);
uint64_t time_get_ms(void);
void time_delay_us(uint64_t us);
void time_delay_ms(uint64_t ms);
void time_delay_func_register(time_delay_func_t func);

#endif /* __TIME_DELAY_H__ */
