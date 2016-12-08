#include<sys/ioctl.h>
#include <stdio.h>

int main (void)
{
	struct winsize w;
	ioctl(0, TIOCGWINSZ, &w);

	printf ("This is %dx%d\n", w.ws_row, w.ws_col);
	return 0;
}
