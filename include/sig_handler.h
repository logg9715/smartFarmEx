#ifndef SIG_HANDLER_H
#define SIG_HANDLER_H

int set_signal_handler(void);
int get_signal_type(const int);
void close_signal_handler(int);

#endif