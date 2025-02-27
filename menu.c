#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <poll.h>
#include <stdbool.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include "menu.h"

#include "pango.h"
#include "render.h"
#include "wayland.h"

// Creates and returns a new menu.
struct menu *menu_create() {
	struct menu *menu = calloc(1, sizeof(struct menu));
	menu->strncmp = strncmp;
	menu->font = "monospace 13";
	menu->normalbg = 0x1f1f28ff;
	menu->normalfg = 0x957fb8ff;
	menu->selectionbg = 0x2e2b47ff;
	menu->selectionfg = 0x7fb4caff;
	menu->selectedbg = 0x353252ff;
	menu->selectedfg = 0xf9cb8cff;
	menu->selectoverbg = 0x3e3a61ff;
	menu->selectoverfg = 0xf083a2ff;
	menu->promptbg = menu->selectionbg;
	menu->promptfg = menu->selectionfg;
	menu->test_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	menu->test_cairo = cairo_create(menu->test_surface);
	menu->line_height = 33;
	return menu;
}

static void free_pages(struct menu *menu) {
	struct page *next = menu->pages;
	while (next) {
		struct page *page = next;
		next = page->next;
		free(page);
	}
}

static void free_items(struct menu *menu) {
	for (size_t i = 0; i < menu->item_count; i++) {
		struct item *item = &menu->items[i];
		free(item->text);
		free(item->stext);
	}
	free(menu->items);
}

// Destroys the menu, freeing memory associated with it.
void menu_destroy(struct menu *menu) {
	free_pages(menu);
	free_items(menu);
	cairo_destroy(menu->test_cairo);
	cairo_surface_destroy(menu->test_surface);
	free(menu->selid);
	free(menu);
}

static bool parse_color(const char *color, uint32_t *result) {
	if (color[0] == '#') {
		++color;
	}
	size_t len = strlen(color);
	if ((len != 6 && len != 8) || !isxdigit(color[0]) || !isxdigit(color[1])) {
		return false;
	}
	char *ptr;
	uint32_t parsed = (uint32_t)strtoul(color, &ptr, 16);
	if (*ptr != '\0') {
		return false;
	}
	*result = len == 6 ? ((parsed << 8) | 0xFF) : parsed;
	return true;
}

// Parse menu options from command line arguments.
void menu_getopts(struct menu *menu, int argc, char *argv[]) {
	const char *usage =
		"Usage: wmenu [-biPvr1] [-f font] [-l lines] [-o output] [-p prompt]\n"
		"\t[-x index] [-e height] [-q query]\n"
		"\t[-N color] [-n color] [-M color] [-m color] [-S color] [-s color]\n"
		"\t[-T color] [-t color] [-U color] [-u color]\n";

	int opt;
	while ((opt = getopt(argc, argv, "bhiPvr1dq:x:e:f:l:o:p:N:n:M:m:S:s:T:t:U:u:")) != -1) {
		switch (opt) {
		case 'b':
			menu->bottom = true;
			break;
		case 'i':
			menu->strncmp = strncasecmp;
			break;
		case 'P':
			menu->passwd = true;
			break;
		case 'v':
			puts("wmenu " VERSION);
			exit(EXIT_SUCCESS);
		case 'f':
			menu->font = optarg;
			break;
		case 'l':
			menu->lines = atoi(optarg);
			break;
		case 'o':
			menu->output_name = optarg;
			break;
		case 'p':
			menu->prompt = optarg;
			break;
		case 'r':
			menu->restricted = true;
			break;
		case '1':
			menu->nomulti = true;
			break;
		case 'x':
			menu->printindex = true;
			menu->restricted = true;
			menu->start_index = atoi(optarg);
			break;
		case 'd':
			menu->nostdin = true;
			break;
		case 'e':
			menu->line_height = atoi(optarg);
			break;
		case 'q':
			strcpy(menu->input, optarg);
			menu->cursor = strlen(menu->input);
			break;
		case 'N':
			if (!parse_color(optarg, &menu->normalbg)) {
				fprintf(stderr, "Invalid background color: %s", optarg);
			}
			break;
		case 'n':
			if (!parse_color(optarg, &menu->normalfg)) {
				fprintf(stderr, "Invalid foreground color: %s", optarg);
			}
			break;
		case 'M':
			if (!parse_color(optarg, &menu->promptbg)) {
				fprintf(stderr, "Invalid prompt background color: %s", optarg);
			}
			break;
		case 'm':
			if (!parse_color(optarg, &menu->promptfg)) {
				fprintf(stderr, "Invalid prompt foreground color: %s", optarg);
			}
			break;
		case 'S':
			if (!parse_color(optarg, &menu->selectionbg)) {
				fprintf(stderr, "Invalid selection background color: %s", optarg);
			}
			break;
		case 's':
			if (!parse_color(optarg, &menu->selectionfg)) {
				fprintf(stderr, "Invalid selection foreground color: %s", optarg);
			}
			break;
		case 'T':
			if (!parse_color(optarg, &menu->selectedbg)) {
				fprintf(stderr, "Invalid selected background color: %s", optarg);
			}
			break;
		case 't':
			if (!parse_color(optarg, &menu->selectedfg)) {
				fprintf(stderr, "Invalid selected foreground color: %s", optarg);
			}
			break;
		case 'U':
			if (!parse_color(optarg, &menu->selectoverbg)) {
				fprintf(stderr, "Invalid selected/over background color: %s", optarg);
			}
			break;
		case 'u':
			if (!parse_color(optarg, &menu->selectoverfg)) {
				fprintf(stderr, "Invalid selected/over foreground color: %s", optarg);
			}
			break;
		default:
			fprintf(stderr, "%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		fprintf(stderr, "%s", usage);
		exit(EXIT_FAILURE);
	}

	int height = get_font_height(menu->font);
	menu->line_height = MAX(menu->line_height, height);
	menu->height = menu->line_height;
	if (menu->lines > 0) {
		menu->height += menu->height * menu->lines;
	}
	menu->padding = height / 2;
}

static bool issel(struct menu *menu, int id){
	for (size_t i = 0; i < menu->selidsize; i++)
		if (menu->selid[i] == id)
			return true;
	return false;
}

// Add an item to the menu.
void menu_add_item(struct menu *menu, char *text) {
	if ((menu->item_count & (menu->item_count - 1)) == 0) {
		size_t alloc_size = menu->item_count ? 2 * menu->item_count : 1;
		void *new_array = realloc(menu->items, sizeof(struct item) * alloc_size);
		if (!new_array) {
			fprintf(stderr, "could not realloc %zu bytes", sizeof(struct item) * alloc_size);
			exit(EXIT_FAILURE);
		}
		menu->items = new_array;
	}

	struct item *new = &menu->items[menu->item_count];
	new->text = text;
	new->id = menu->item_count;

	char *sep;
	if ((sep = strchr(text, '\t')))
		new->stext = strndup(text, sep - text);
	else
		new->stext = strdup(text);

	menu->item_count++;
}

static int compare_items(const void *a, const void *b) {
	const struct item *item_a = a;
	const struct item *item_b = b;
	return strcmp(item_a->text, item_b->text);
}

void menu_sort_and_deduplicate(struct menu *menu) {
	size_t j = 1;
	size_t i;

	qsort(menu->items, menu->item_count, sizeof(*menu->items), compare_items);

	for (i = 1; i < menu->item_count; i++) {
		if (strcmp(menu->items[i].text, menu->items[j - 1].text) == 0) {
			free(menu->items[i].text);
		} else {
			menu->items[j] = menu->items[i];
			j++;
		}
	}
	menu->item_count = j;
}

static void append_page(struct page *page, struct page **first, struct page **last) {
	if (*last) {
		(*last)->next = page;
	} else {
		*first = page;
	}
	page->prev = *last;
	page->next = NULL;
	*last = page;
}

static void page_items(struct menu *menu) {
	// Free existing pages
	while (menu->pages != NULL) {
		struct page *page = menu->pages;
		menu->pages = menu->pages->next;
		free(page);
	}

	if (!menu->matches) {
		return;
	}

	// Make new pages
	if (menu->lines > 0) {
		struct page *pages_end = NULL;
		struct item *item = menu->matches;
		while (item) {
			struct page *page = calloc(1, sizeof(struct page));
			page->first = item;

			for (int i = 1; item && i <= menu->lines; i++) {
				item->page = page;
				page->last = item;
				item = item->next_match;
			}
			append_page(page, &menu->pages, &pages_end);
		}
	} else {
		// Calculate available space
		int max_width = menu->width - menu->inputw - menu->promptw
			- menu->left_arrow - menu->right_arrow;

		struct page *pages_end = NULL;
		struct item *item = menu->matches;
		while (item) {
			struct page *page = calloc(1, sizeof(struct page));
			page->first = item;

			int total_width = 0;
			int items = 0;
			while (item) {
				total_width += item->width + 2 * menu->padding;
				if (total_width > max_width && items > 0) {
					break;
				}
				items++;

				item->page = page;
				page->last = item;
				item = item->next_match;
			}
			append_page(page, &menu->pages, &pages_end);
		}
	}
}

static const char *fstrstr(struct menu *menu, const char *s, const char *sub) {
	for (size_t len = strlen(sub); *s; s++) {
		if (!menu->strncmp(s, sub, len)) {
			return s;
		}
	}
	return NULL;
}

static void append_match(struct item *item, struct item **first, struct item **last) {
	if (*last) {
		(*last)->next_match = item;
	} else {
		*first = item;
	}
	item->prev_match = *last;
	item->next_match = NULL;
	*last = item;
}

static void match_items(struct menu *menu) {
	struct item *lexact = NULL, *exactend = NULL;
	struct item *lprefix = NULL, *prefixend = NULL;
	struct item *lsubstr  = NULL, *substrend = NULL;
	char buf[sizeof menu->input], *tok;
	char **tokv = NULL;
	int i, tokc = 0;
	size_t k;
	size_t tok_len;
	menu->matches = NULL;
	menu->matches_end = NULL;
	menu->sel = NULL;

	size_t input_len = strlen(menu->input);

	/* tokenize input by space for matching the tokens individually */
	strcpy(buf, menu->input);
	tok = strtok(buf, " ");
	while (tok) {
		tokv = realloc(tokv, (tokc + 1) * sizeof *tokv);
		if (!tokv) {
			fprintf(stderr, "could not realloc %zu bytes",
					(tokc + 1) * sizeof *tokv);
			exit(EXIT_FAILURE);
		}
		tokv[tokc] = tok;
		tokc++;
		tok = strtok(NULL, " ");
	}
	tok_len = tokc ? strlen(tokv[0]) : 0;

	for (k = 0; k < menu->item_count; k++) {
		struct item *item = &menu->items[k];
		for (i = 0; i < tokc; i++) {
			if (!fstrstr(menu, item->text, tokv[i])) {
				/* token does not match */
				break;
			}
		}
		if (i != tokc) {
			/* not all tokens match */
			continue;
		}
		if (!tokc || !menu->strncmp(menu->input, item->text, input_len + 1)) {
			append_match(item, &lexact, &exactend);
		} else if (!menu->strncmp(tokv[0], item->text, tok_len)) {
			append_match(item, &lprefix, &prefixend);
		} else {
			append_match(item, &lsubstr, &substrend);
		}
	}

	free(tokv);

	if (lexact) {
		menu->matches = lexact;
		menu->matches_end = exactend;
	}
	if (lprefix) {
		if (menu->matches_end) {
			menu->matches_end->next_match = lprefix;
			lprefix->prev_match = menu->matches_end;
		} else {
			menu->matches = lprefix;
		}
		menu->matches_end = prefixend;
	}
	if (lsubstr) {
		if (menu->matches_end) {
			menu->matches_end->next_match = lsubstr;
			lsubstr->prev_match = menu->matches_end;
		} else {
			menu->matches = lsubstr;
		}
		menu->matches_end = substrend;
	}

	page_items(menu);
	if (menu->pages) {
		menu->sel = menu->pages->first;
	}
}

// Render menu items.
void menu_render_items(struct menu *menu) {
	calc_widths(menu);
	match_items(menu);
	render_menu(menu);
}

static void insert(struct menu *menu, const char *text, ssize_t len) {
	if (strlen(menu->input) + len > sizeof menu->input - 1) {
		return;
	}
	memmove(menu->input + menu->cursor + len, menu->input + menu->cursor,
			sizeof menu->input - menu->cursor - MAX(len, 0));
	if (len > 0 && text != NULL) {
		memcpy(menu->input + menu->cursor, text, len);
	}
	menu->cursor += len;
}

// Add pasted text to the menu input.
void menu_paste(struct menu *menu, const char *text, ssize_t len) {
	insert(menu, text, len);
}

static size_t nextrune(struct menu *menu, int incr) {
	size_t n, len;

	len = strlen(menu->input);
	for(n = menu->cursor + incr; n < len && (menu->input[n] & 0xc0) == 0x80; n += incr);
	return n;
}

// Move the cursor to the beginning or end of the word, skipping over any preceding whitespace.
static void movewordedge(struct menu *menu, int dir) {
	if (dir < 0) {
		// Move to beginning of word
		while (menu->cursor > 0 && menu->input[nextrune(menu, -1)] == ' ') {
			menu->cursor = nextrune(menu, -1);
		}
		while (menu->cursor > 0 && menu->input[nextrune(menu, -1)] != ' ') {
			menu->cursor = nextrune(menu, -1);
		}
	} else {
		// Move to end of word
		size_t len = strlen(menu->input);
		while (menu->cursor < len && menu->input[menu->cursor] == ' ') {
			menu->cursor = nextrune(menu, +1);
		}
		while (menu->cursor < len && menu->input[menu->cursor] != ' ') {
			menu->cursor = nextrune(menu, +1);
		}
	}
}

// Handle a keypress.
void menu_keypress(struct menu *menu, enum wl_keyboard_key_state key_state,
		xkb_keysym_t sym) {
	if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		return;
	}

	struct xkb_state *state = context_get_xkb_state(menu->context);
	bool ctrl = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
			XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
	bool meta = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_ALT,
			XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
	bool shift = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
			XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);

	size_t len = strlen(menu->input);

	if (ctrl) {
		// Emacs-style line editing bindings
		switch (sym) {
		case XKB_KEY_a:
			sym = XKB_KEY_Home;
			break;
		case XKB_KEY_b:
			sym = XKB_KEY_Left;
			break;
		case XKB_KEY_c:
			sym = XKB_KEY_Escape;
			break;
		case XKB_KEY_d:
			sym = XKB_KEY_Delete;
			break;
		case XKB_KEY_e:
			sym = XKB_KEY_End;
			break;
		case XKB_KEY_f:
			sym = XKB_KEY_Right;
			break;
		case XKB_KEY_g:
			sym = XKB_KEY_Escape;
			break;
		case XKB_KEY_bracketleft:
			sym = XKB_KEY_Escape;
			break;
		case XKB_KEY_i:
			sym = XKB_KEY_Tab;
			break;
		case XKB_KEY_h:
		case XKB_KEY_H:
			sym = XKB_KEY_Left;
			break;
		case XKB_KEY_j:
		case XKB_KEY_J:
			sym = XKB_KEY_Down;
			break;
		case XKB_KEY_l:
		case XKB_KEY_L:
			sym = XKB_KEY_Right;
			break;
		case XKB_KEY_k:
		case XKB_KEY_K:
			sym = XKB_KEY_Up;
			break;
		case XKB_KEY_m:
		case XKB_KEY_M:
			sym = XKB_KEY_Return;
			ctrl = false;
			break;
		case XKB_KEY_n:
			sym = XKB_KEY_Down;
			break;
		case XKB_KEY_p:
			sym = XKB_KEY_Up;
			break;

		case XKB_KEY_o:
			// Delete right
			menu->input[menu->cursor] = '\0';
			match_items(menu);
			render_menu(menu);
			return;
		case XKB_KEY_u:
			// Delete left
			insert(menu, NULL, 0 - menu->cursor);
			match_items(menu);
			render_menu(menu);
			return;
		case XKB_KEY_w:
			// Delete word
			while (menu->cursor > 0 && menu->input[nextrune(menu, -1)] == ' ') {
				insert(menu, NULL, nextrune(menu, -1) - menu->cursor);
			}
			while (menu->cursor > 0 && menu->input[nextrune(menu, -1)] != ' ') {
				insert(menu, NULL, nextrune(menu, -1) - menu->cursor);
			}
			match_items(menu);
			render_menu(menu);
			return;
		case XKB_KEY_Y:
			// Paste clipboard
			if (!context_paste(menu->context)) {
				return;
			}
			match_items(menu);
			render_menu(menu);
			return;
		case XKB_KEY_Left:
		case XKB_KEY_KP_Left:
			movewordedge(menu, -1);
			render_menu(menu);
			return;
		case XKB_KEY_Right:
		case XKB_KEY_KP_Right:
			movewordedge(menu, +1);
			render_menu(menu);
			return;

		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			break;
		default:
			return;
		}
	} else if (meta) {
		// Emacs-style line editing bindings
		switch (sym) {
		case XKB_KEY_b:
			movewordedge(menu, -1);
			render_menu(menu);
			return;
		case XKB_KEY_f:
			movewordedge(menu, +1);
			render_menu(menu);
			return;
		case XKB_KEY_g:
			sym = XKB_KEY_Home;
			break;
		case XKB_KEY_G:
			sym = XKB_KEY_End;
			break;
		case XKB_KEY_h:
			sym = XKB_KEY_Up;
			break;
		case XKB_KEY_j:
			sym = XKB_KEY_Next;
			break;
		case XKB_KEY_k:
			sym = XKB_KEY_Prior;
			break;
		case XKB_KEY_l:
			sym = XKB_KEY_Down;
			break;
		default:
			return;
		}
	}

	char buf[8];
	switch (sym) {
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter:
		if (shift) {
			if (menu->restricted)
				break;
			menu_print_and_exit(menu, true);
		} else if (ctrl) {
			if (menu->nomulti)
				break;
			if (menu->sel && issel(menu, menu->sel->id)) {
				for (size_t i = 0; i < menu->selidsize; i++)
					if (menu->selid[i] == menu->sel->id) {
						menu->selid[i] = -1;
						break;
					}
				menu->selcount--;
			} else {
				size_t i;
				for (i = 0; i < menu->selidsize; i++)
					if (menu->selid[i] == -1) {
						menu->selid[i] = menu->sel->id;
						break;
					}
				if (i == menu->selidsize) {
					menu->selid = realloc(menu->selid, (menu->selidsize+1) * sizeof(int));
					menu->selid[menu->selidsize] = menu->sel->id;
					menu->selidsize++;
				}
				menu->selcount++;
			}
			if (menu->cursor < len)
				menu->cursor = nextrune(menu, +1);
			else if (menu->sel && menu->sel->next_match)
				menu->sel = menu->sel->next_match;

			render_menu(menu);
		} else if (menu->sel || !menu->restricted || menu->selcount > 0) {
			menu_print_and_exit(menu, false);
		}
		break;
	case XKB_KEY_Left:
	case XKB_KEY_KP_Left:
	case XKB_KEY_Up:
	case XKB_KEY_KP_Up:
		if (menu->sel && menu->sel->prev_match) {
			menu->sel = menu->sel->prev_match;
			render_menu(menu);
		} else if (menu->cursor > 0) {
			menu->cursor = nextrune(menu, -1);
			render_menu(menu);
		}
		break;
	case XKB_KEY_Right:
	case XKB_KEY_KP_Right:
	case XKB_KEY_Down:
	case XKB_KEY_KP_Down:
		if (menu->cursor < len) {
			menu->cursor = nextrune(menu, +1);
			render_menu(menu);
		} else if (menu->sel && menu->sel->next_match) {
			menu->sel = menu->sel->next_match;
			render_menu(menu);
		}
		break;
	case XKB_KEY_Prior:
	case XKB_KEY_KP_Prior:
		if (menu->sel && menu->sel->page->prev) {
			menu->sel = menu->sel->page->prev->first;
			render_menu(menu);
		}
		break;
	case XKB_KEY_Next:
	case XKB_KEY_KP_Next:
		if (menu->sel && menu->sel->page->next) {
			menu->sel = menu->sel->page->next->first;
			render_menu(menu);
		}
		break;
	case XKB_KEY_Home:
	case XKB_KEY_KP_Home:
		if (menu->sel == menu->matches) {
			menu->cursor = 0;
			render_menu(menu);
		} else {
			menu->sel = menu->matches;
			render_menu(menu);
		}
		break;
	case XKB_KEY_End:
	case XKB_KEY_KP_End:
		if (menu->cursor < len) {
			menu->cursor = len;
			render_menu(menu);
		} else {
			menu->sel = menu->matches_end;
			render_menu(menu);
		}
		break;
	case XKB_KEY_BackSpace:
		if (menu->cursor > 0) {
			insert(menu, NULL, nextrune(menu, -1) - menu->cursor);
			match_items(menu);
			render_menu(menu);
		}
		break;
	case XKB_KEY_Delete:
	case XKB_KEY_KP_Delete:
		if (menu->cursor == len) {
			return;
		}
		menu->cursor = nextrune(menu, +1);
		insert(menu, NULL, nextrune(menu, -1) - menu->cursor);
		match_items(menu);
		render_menu(menu);
		break;
	case XKB_KEY_Tab:
		if (!menu->sel) {
			return;
		}
		menu->cursor = strnlen(menu->sel->text, sizeof menu->input - 1);
		memcpy(menu->input, menu->sel->text, menu->cursor);
		menu->input[menu->cursor] = '\0';
		match_items(menu);
		render_menu(menu);
		break;
	case XKB_KEY_Escape:
		menu->exit = true;
		menu->failure = true;
		break;
	default:
		if (xkb_keysym_to_utf8(sym, buf, 8)) {
			insert(menu, buf, strnlen(buf, 8));
			match_items(menu);
			render_menu(menu);
		}
	}
}

void menu_print_and_exit(struct menu *menu, bool print_query) {
	if (print_query || (!menu->sel && menu->selcount == 0)) {
		puts(menu->input);
	} else {
		if (menu->selcount == 0)
			if (menu->printindex)
				printf("%d\n", menu->sel->id + menu->start_index);
			else
				puts(menu->sel->text);
		else
			for (size_t i = 0; i < menu->selidsize; i++)
				if (menu->selid[i] != -1) {
					if (menu->printindex)
						printf("%d\n", menu->items[menu->selid[i]].id + menu->start_index);
					else
						puts(menu->items[menu->selid[i]].text);
				}
	}

	fflush(stdout);
	menu->exit = true;
}
