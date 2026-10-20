#include "system.h"
#include "scheduler.h"

static void
_thread_(void *arg)
{
	const char *name;
	int i;

	name = (const char *)arg;
	for (i = 0; i < 4; ++i) {
		printf("%s %d\n", name, i);
		fflush(stdout);
		us_sleep(500000);
	}
}

int
main(int argc, char *argv[])
{
	UNUSED(argc);
	UNUSED(argv);

	printf("extra credit: no scheduler_yield(); expect a switch about once a second\n");
	fflush(stdout);

	if (scheduler_create(_thread_, "alpha") ||
	    scheduler_create(_thread_, "beta") ||
	    scheduler_create(_thread_, "gamma")) {
		TRACE(0);
		return -1;
	}
	scheduler_execute();
	return 0;
}
