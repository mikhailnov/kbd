#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <unistd.h>

#include "nls.h"
#include "kbd.h"
#include "keymap.h"
#include "ksyms.h"

static int
defkeys(struct keymap *kmap, int fd, int kbd_mode)
{
	struct kbentry ke;
	int ct = 0;
	int i, j, fail;

	if (kmap->flags & LKFLAG_UNICODE_MODE) {
		/* temporarily switch to K_UNICODE while defining keys */
		if (ioctl(fd, KDSKBMODE, K_UNICODE)) {
			log_error(kmap, _("KDSKBMODE: %s: could not switch to Unicode mode"),
				strerror(errno));
			goto fail;
		}
	}

	for (i = 0; i < MAX_NR_KEYMAPS; i++) {
		if (kmap->key_map[i]) {
			for (j = 0; j < NR_KEYS; j++) {
				if (!((kmap->keymap_was_set[i])[j]))
					continue;

				ke.kb_index = j;
				ke.kb_table = i;
				ke.kb_value = (kmap->key_map[i])[j];

				fail = ioctl(fd, KDSKBENT, (unsigned long)&ke);

				if (fail) {
					if (errno == EPERM) {
						log_error(kmap, _("Keymap %d: Permission denied"), i);
						j = NR_KEYS;
						continue;
					}
					log_error(kmap, "%s", strerror(errno));
				} else
					ct++;

				log_verbose(kmap, LOG_VERBOSE1, _("keycode %d, table %d = %d%s"),
					j, i, (kmap->key_map[i])[j], fail ? _("    FAILED") : "");

				if (fail && kmap->verbose > 1)
					log_error(kmap, _("failed to bind key %d to value %d"),
						j, (kmap->key_map[i])[j]);
			}

		} else if (kmap->keymaps_line_seen && !kmap->defining[i]) {
			/* deallocate keymap */
			ke.kb_index = 0;
			ke.kb_table = i;
			ke.kb_value = K_NOSUCHMAP;

			log_verbose(kmap, LOG_VERBOSE2, _("deallocate keymap %d"), i);

			if (ioctl(fd, KDSKBENT, (unsigned long)&ke)) {
				if (errno != EINVAL) {
					log_error(kmap, _("KDSKBENT: %s: could not deallocate keymap %d"),
						strerror(errno), i);
					goto fail;
				}
				/* probably an old kernel */
				/* clear keymap by hand */
				for (j = 0; j < NR_KEYS; j++) {
					ke.kb_index = j;
					ke.kb_table = i;
					ke.kb_value = K_HOLE;

					if (ioctl(fd, KDSKBENT, (unsigned long)&ke)) {
						if (errno == EINVAL && i >= 16)
							break;	/* old kernel */

						log_error(kmap, _("KDSKBENT: %s: cannot deallocate or clear keymap"),
							strerror(errno));
						goto fail;
					}
				}
			}
		}
	}

	if ((kmap->flags & LKFLAG_UNICODE_MODE) && ioctl(fd, KDSKBMODE, kbd_mode)) {
		log_error(kmap, _("KDSKBMODE: %s: could not return to original keyboard mode"),
			strerror(errno));
		goto fail;
	}

	return ct;

 fail:	return -1;
}


static char *
ostr(struct keymap *kmap, char *s)
{
	int lth = strlen(s);
	char *ns0 = malloc(4 * lth + 1);
	char *ns = ns0;

	if (ns == NULL) {
		log_error(kmap, _("out of memory"));
		return NULL;
	}

	while (*s) {
		switch (*s) {
		case '\n':
			*ns++ = '\\';
			*ns++ = 'n';
			break;
		case '\033':
			*ns++ = '\\';
			*ns++ = '0';
			*ns++ = '3';
			*ns++ = '3';
			break;
		default:
			*ns++ = *s;
		}
		s++;
	}
	*ns = 0;
	return ns0;
}

static int
deffuncs(struct keymap *kmap, int fd)
{
	int i, ct = 0;
	char *ptr, *s;
	struct kbsentry kbs;

	for (i = 0; i < MAX_NR_FUNC; i++) {
		kbs.kb_func = i;

		if ((ptr = kmap->func_table[i])) {
			strcpy((char *)kbs.kb_string, ptr);
			if (ioctl(fd, KDSKBSENT, (unsigned long)&kbs)) {
				s = ostr(kmap, (char *)kbs.kb_string);
				if (s == NULL)
					return -1;
				log_error(kmap, _("failed to bind string '%s' to function %s"),
					s, syms[KT_FN].table[kbs.kb_func]);
				free(s);
			} else {
				ct++;
			}
		} else if (kmap->flags & LKFLAG_CLEAR_STRINGS) {
			kbs.kb_string[0] = 0;

			if (ioctl(fd, KDSKBSENT, (unsigned long)&kbs)) {
				log_error(kmap, _("failed to clear string %s"),
					syms[KT_FN].table[kbs.kb_func]);
			} else {
				ct++;
			}
		}
	}
	return ct;
}

static int
defdiacs(struct keymap *kmap, int fd)
{
	unsigned int i, count;
	struct kbdiacrs kd;
#ifdef KDSKBDIACRUC
	struct kbdiacrsuc kdu;
#endif

	count = kmap->accent_table_size;
	if (count > MAX_DIACR) {
		count = MAX_DIACR;
		log_error(kmap, _("too many compose definitions"));
	}
#ifdef KDSKBDIACRUC
	if (kmap->prefer_unicode) {
		kdu.kb_cnt = count;

		for (i = 0; i < kdu.kb_cnt; i++) {
			kdu.kbdiacruc[i].diacr  = kmap->accent_table[i].diacr;
			kdu.kbdiacruc[i].base   = kmap->accent_table[i].base;
			kdu.kbdiacruc[i].result = kmap->accent_table[i].result;
		}

		if (ioctl(fd, KDSKBDIACRUC, (unsigned long)&kdu))
			goto fail2;
	} else
#endif
	{
		kd.kb_cnt = count;
		for (i = 0; i < kd.kb_cnt; i++) {
			kd.kbdiacr[i].diacr  = kmap->accent_table[i].diacr;
			kd.kbdiacr[i].base   = kmap->accent_table[i].base;
			kd.kbdiacr[i].result = kmap->accent_table[i].result;
		}

		if (ioctl(fd, KDSKBDIACR, (unsigned long)&kd))
			goto fail1;
	}

	return kd.kb_cnt;

 fail1:	log_error(kmap, "KDSKBDIACR: %s", strerror(errno));
	return -1;

#ifdef KDSKBDIACRUC
 fail2:	log_error(kmap, "KDSKBDIACRUC: %s", strerror(errno));
	return -1;
#endif
}

int
lk_load_keymap(struct keymap *kmap, int fd, int kbd_mode)
{
	int keyct, funcct, diacct;

	if (lk_add_constants(kmap) < 0)
		return -1;

	if ((keyct = defkeys(kmap, fd, kbd_mode)) < 0 || (funcct = deffuncs(kmap, fd)) < 0)
		return -1;

	log_verbose(kmap, LOG_VERBOSE1, _("\nChanged %d %s and %d %s"),
		keyct, (keyct == 1) ? _("key") : _("keys"),
		funcct, (funcct == 1) ? _("string") : _("strings"));

	if (kmap->accent_table_size > 0 || kmap->flags & LKFLAG_CLEAR_COMPOSE) {
		diacct = defdiacs(kmap, fd);

		if (diacct < 0)
			return -1;

		log_verbose(kmap, LOG_VERBOSE1, _("Loaded %d compose %s"),
			diacct, (diacct == 1) ? _("definition") : _("definitions"));

	} else {
		log_verbose(kmap, LOG_VERBOSE1, _("(No change in compose definitions)"));
	}

	return 0;
}
