#include <stdint.h>
#include <stddef.h>
struct host_guest_image { uint32_t base, end; };
extern struct host_guest_image host_image;
#define HOST_LOG_WARN 5
#define HOST_LOG_ERROR 6
void host_logf(int priority, const char *format, ...);
