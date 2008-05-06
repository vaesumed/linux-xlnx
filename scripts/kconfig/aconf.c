/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Copyright (C) 2008 Sam Ravnborg <sam@ravnborg.org>
 * Released under the terms of the GNU GPL v2.0.
 */

/*
 * Generate the automated configs
 */

#include <locale.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

static void check_conf(struct menu *menu);
static void conf(struct menu *menu);

enum {
	set_default,
	set_yes,
	set_mod,
	set_no,
	set_random
} default_value;


static int conf_cnt;
static struct menu *rootEntry;

/* Set strig value - it this a nop as it looks like? */
static void conf_string(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	const char *def;


	if (!sym_is_changable(sym))
		return;

	if (sym_has_value(sym) && (default_value != set_default))
		return;

	def = sym_get_string_value(sym);
	if (def)
		sym_set_string_value(sym, def);
}

static void conf_sym(struct menu *menu)
{
	struct symbol *sym = menu->sym;
	int type;
	tristate val;

	if (!sym_is_changable(sym))
		return;

	if (sym_has_value(sym) && (default_value != set_default))
		return;

	type = sym_get_type(sym);
	switch (default_value) {
	case set_yes:
		if (sym_tristate_within_range(sym, yes)) {
			sym_set_tristate_value(sym, yes);
			break;
		}
	/* fallthrough */
	case set_mod:
		if (type == S_TRISTATE) {
			if (sym_tristate_within_range(sym, mod)) {
				sym_set_tristate_value(sym, mod);
				break;
			}
		} else if (sym_tristate_within_range(sym, yes)) {
			sym_set_tristate_value(sym, yes);
			break;
		}
	/* fallthrough */
	case set_no:
		if (sym_tristate_within_range(sym, no)) {
			sym_set_tristate_value(sym, no);
			break;
		}
	/* fallthrough */
	case set_random:
		 do {
			val = (tristate)(rand() % 3);
		} while (!sym_tristate_within_range(sym, val));
		switch (val) {
		case no:  sym_set_tristate_value(sym, no); break;
		case mod: sym_set_tristate_value(sym, mod); break;
		case yes: sym_set_tristate_value(sym, yes); break;
		}
		break;
	case set_default:
		sym_set_tristate_value(sym, sym_get_tristate_value(sym));
		break;
	}
}

static void conf_choice(struct menu *menu)
{
	struct symbol *sym, *def_sym;
	struct menu *child;
	int type;
	bool is_new;
	int cnt, def;

	sym = menu->sym;
	type = sym_get_type(sym);
	is_new = !sym_has_value(sym);
	if (sym_is_changable(sym)) {
		conf_sym(menu);
		sym_calc_value(sym);
	}
	if (sym_get_tristate_value(sym) != yes)
		return;
	def_sym = sym_get_choice_value(sym);
	cnt = def = 0;
	for (child = menu->list; child; child = child->next) {
		if (!child->sym || !menu_is_visible(child))
			continue;
		cnt++;
		if (child->sym == def_sym)
			def = cnt;
	}
	if (cnt == 1)
		goto conf_childs;

	switch (default_value) {
	case set_random:
		if (is_new)
			def = (rand() % cnt) + 1;
	/* fallthrough */
	case set_default:
	case set_yes:
	case set_mod:
	case set_no:
		cnt = def;
		break;
	}

conf_childs:
	for (child = menu->list; child; child = child->next) {
		if (!child->sym || !menu_is_visible(child))
			continue;
		if (!--cnt)
			break;
	}
	sym_set_choice_value(sym, child->sym);
	for (child = child->list; child; child = child->next)
		conf(child);
}


static void conf(struct menu *menu)
{
	struct symbol *sym;
	struct property *prop;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	prop = menu->prompt;
	if (prop && prop->type == P_MENU) {
		if (menu != rootEntry) {
			check_conf(menu);
			return;
		}
	}

	if (!sym)
		goto conf_childs;

	if (sym_is_choice(sym)) {
		conf_choice(menu);
		if (sym->curr.tri != mod)
			return;
		goto conf_childs;
	}

	switch (sym->type) {
	case S_INT:
	case S_HEX:
	case S_STRING:
		conf_string(menu);
		break;
	default:
		conf_sym(menu);
		break;
	}

conf_childs:
	for (child = menu->list; child; child = child->next)
		conf(child);
}

static void check_conf(struct menu *menu)
{
	struct symbol *sym;
	struct menu *child;

	if (!menu_is_visible(menu))
		return;

	sym = menu->sym;
	if (sym && !sym_has_value(sym)) {
		if (sym_is_changable(sym) ||
		    (sym_is_choice(sym) &&
		     sym_get_tristate_value(sym) == yes)) {
			conf_cnt++;
			rootEntry = menu_get_parent_menu(menu);
			conf(rootEntry);
		}
	}

	for (child = menu->list; child; child = child->next)
		check_conf(child);
}

static void usage(void)
{
	printf(_("usage: aconf COMMAND [-b config_file] Kconfig\n"));
	printf("\n");
	printf(_("The supported commands are:\n"));
	printf(_("   allnoconfig  set as many values as possible to 'n'\n"));
	printf(_("   allyesconfig set as many values as possible to 'y'\n"));
	printf(_("   allmodconfig set as many values as possible to 'm'\n"));
	printf(_("   alldefconfig set all vaues to their default value\n"));
	printf(_("   randconfig   select a random value for all value\n"));
	printf("\n");
	printf(_("   -b file      optional base configuration\n"));
	printf(_("   Kconfig      the kconfig configuration\n"));
	printf("\n");
	printf(_("   Output is stored in .config (if not overridden by KCONFIG_CONFIG)\n"));
	printf("\n");
}

int main(int ac, char **av)
{
	char *config_filename;
	char *config_file = NULL;
	char *kconfig_file = NULL;
	struct stat tmpstat;

	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	if (ac < 2) {
		usage();
		exit(1);
	}
	if (strcmp(av[1], "allnoconfig") == 0)
		default_value = set_no;
	else if (strcmp(av[1], "allyesconfig") == 0)
		default_value = set_yes;
	else if (strcmp(av[1], "allmodconfig") == 0)
		default_value = set_mod;
	else if (strcmp(av[1], "alldefconfig") == 0)
		default_value = set_default;
	else if (strcmp(av[1], "randconfig") == 0) {
		default_value = set_random;
		srand(time(NULL));
	} else {
		usage();
		exit(1);
	}
	if (strcmp(av[2], "-b") == 0) {
		config_file = av[3];
		kconfig_file = av[4];
	} else {
		kconfig_file = av[2];
	}
	if (!kconfig_file) {
		fprintf(stderr, _("%s: Kconfig file missing\n"), av[0]);
		exit(1);
	}
	conf_parse(kconfig_file);
	/* debug: zconfdump(stdout); */
	if (strcmp(config_file, "-")) {
		if (config_file && stat(config_file, &tmpstat)) {
			fprintf(stderr, _("%s: failed to open %s\n"),
			        av[0], config_file);
			exit(1);
		}
		config_filename = config_file;
	} else {
		config_filename = "stdin";
	}

	if (config_file && conf_read_simple(config_file, S_DEF_USER)) {
		fprintf(stderr, _("%s: failed to read %s\n"),
		        av[0], config_filename);
		exit(1);
	}
	if (config_file) {
		printf("#\n");
		printf(_("# configuration is based on '%s'\n"),
		       config_filename);
	}
	/* generate the config */
	do {
		conf_cnt = 0;
		check_conf(&rootmenu);
	} while (conf_cnt);
	/* write out the config */
	if (conf_write(NULL) || conf_write_autoconf()) {
		fprintf(stderr,
		        _("%s: error during write of the configuration.\n"),
		        av[0]);
		exit(1);
	}
	return 0;
}
