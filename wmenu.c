#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "menu.h"
#include "wayland.h"

static void read_items(struct menu *menu) {
	char buf[sizeof menu->input];
	while (fgets(buf, sizeof buf, stdin)) {
		char *p = strchr(buf, '\n');
		if (p) {
			*p = '\0';
		}
		menu_add_item(menu, strdup(buf));
	}
}

int main(int argc, char *argv[]) {
	struct menu *menu = menu_create();
	menu_getopts(menu, argc, argv);
	if (!menu->nostdin)
		read_items(menu);
	int status = menu_run(menu);
	menu_destroy(menu);
	return status;
}
