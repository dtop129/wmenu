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

static void exit_cmd(struct menu *menu, char *text, bool print_text) {
	if (print_text) {
		puts(text);
	} else {
		for (size_t i = 0; i < menu->selidsize; i++)
			if (menu->selid[i] != -1)
				puts(menu->items[menu->selid[i]].text);

		if (menu->selcount == 0)
			puts(text);
	}

	fflush(stdout);
	menu->exit = true;
}

int main(int argc, char *argv[]) {
	struct menu *menu = menu_create(exit_cmd);
	menu_getopts(menu, argc, argv);
	read_items(menu);
	int status = menu_run(menu);
	menu_destroy(menu);
	return status;
}
