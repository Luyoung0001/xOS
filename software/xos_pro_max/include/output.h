#ifndef OUTPUT_H
#define OUTPUT_H
/* Output target control */
typedef enum {
    OUTPUT_UART = 0x01,
    OUTPUT_HDMI = 0x02,
    OUTPUT_BOTH = 0x03
} output_target_t;

extern output_target_t output_target;
void set_output_target(output_target_t target);
output_target_t get_output_target(void);


#endif /* OUTPUT_H */
