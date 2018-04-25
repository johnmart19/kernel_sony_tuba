#define ENABLED 1
#define DISABLED 0

#define POWER_SAVER 0
#define BALANCED 1
#define TURBO 2

#define HOTPLUG_PERCORE 1
#define HOTPLUG_SCHED 2

extern int sched_set_boost(int enable);

