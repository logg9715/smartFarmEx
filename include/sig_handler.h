#ifndef SIG_HANDLER_H
#define SIG_HANDLER_H

int set_signal_handler(void);
int check_signal_term(const int);
void close_signal_handler(int);

#endif