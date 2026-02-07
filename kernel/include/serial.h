#ifndef SERIAL_H
#define SERIAL_H

/* Serial port operations */
void serial_init(void);
void serial_write_char(char c);
void serial_write(const char* s);

#endif /* SERIAL_H */
