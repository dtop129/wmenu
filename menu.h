#ifndef WMENU_MENU_H
#define WMENU_MENU_H

#include <cairo/cairo.h>
#include <stdbool.h>
#include <sys/types.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-client.h>

struct menu;

// A menu item.
struct item {
	char *text;
	char *stext;
	int width;
	struct item *prev_match; // previous matching item
	struct item *next_match; // next matching item
	struct page *page;       // the page holding this item

	int id;
};

// A page of menu items.
struct page {
	struct item *first; // first item in the page
	struct item *last;  // last item in the page
	struct page *prev;  // previous page
	struct page *next;  // next page
};

// Menu state.
struct menu {
	// Whether the menu appears at the bottom of the screen
	bool bottom;
	// The function used to match menu items
	int (*strncmp)(const char *, const char *, size_t);
	// Whether the input is a password
	bool passwd;
	// Whether to only allow results coming from stdin
	bool restricted;
	// Whether multi-selection is allowed
	bool nomulti;
	// Whether to print the selection index
	bool printindex;
	int start_index;
	// The font used to display the menu
	char *font;
	// The number of lines to list items vertically
	int lines;
	// The name of the output to display on
	char *output_name;
	// The prompt displayed to the left of the input field
	char *prompt;
	// Normal colors
	uint32_t normalbg, normalfg;
	// Prompt colors
	uint32_t promptbg, promptfg;
	// Selection colors
	uint32_t selectionbg, selectionfg;
	// Selected colors
	uint32_t selectedbg, selectedfg;
	// Selected/over colors
	uint32_t selectoverbg, selectoverfg;

	struct wl_context *context;

	// 1x1 surface used estimate text sizes with pango
	cairo_surface_t *test_surface;
	cairo_t *test_cairo;

	int width;
	int height;
	int line_height;
	int padding;
	int inputw;
	int promptw;
	int left_arrow;
	int right_arrow;

	char input[BUFSIZ];
	size_t cursor;

	struct item *items;       // array of all items
	size_t item_count;
	struct item *matches;     // list of matching items
	struct item *matches_end; // last matching item
	struct item *sel;         // selected item
	struct page *pages;       // list of pages

	int *selid;
	size_t selidsize;
	size_t selcount;

	bool exit;
	bool failure;
};

struct menu *menu_create();
void menu_destroy(struct menu *menu);
void menu_getopts(struct menu *menu, int argc, char *argv[]);
void menu_add_item(struct menu *menu, char *text);
void menu_sort_and_deduplicate(struct menu *menu);
void menu_render_items(struct menu *menu);
void menu_paste(struct menu *menu, const char *text, ssize_t len);
void menu_keypress(struct menu *menu, enum wl_keyboard_key_state key_state,
		xkb_keysym_t sym);
void menu_print_and_exit(struct menu *menu, bool print_query);

#endif
