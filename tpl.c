#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <unistd.h>

#include "trealla.h"
#include "builtins.h"
#include "history.h"
#include "library.h"
#include "module.h"
#include "network.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define snprintf _snprintf
#define isatty _isatty
#define msleep Sleep
#else
#include <sys/stat.h>
#include <sys/types.h>
#ifndef __wasi__
#include <sys/wait.h>
#endif
#define msleep(ms)                                                     \
{                                                                      \
	struct timespec tv;                                                \
	tv.tv_sec = (ms) / 1000;                                           \
	tv.tv_nsec = ((ms) % 1000) * 1000 * 1000;                          \
	nanosleep(&tv, &tv);                                               \
}
#endif

#ifndef __wasi__
static int daemonize(int argc, char *argv[])
{
	char path[1024];
	path[0] = '\0';

#ifndef _WIN32
	int watchdog = 0;
#endif

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--watchdog")) {
#ifndef _WIN32
			watchdog = 1;
#endif
		} else if (!strncmp(argv[i], "--cd=", 5))
			snprintf(path, sizeof(path), "%s", argv[i] + 5);
	}

#ifdef _WIN32
	char cmd[1024], args[1024 * 8];
	size_t args_len = 0;
	args[0] = 0;
	snprintf(cmd, sizeof(cmd), "%s.exe", argv[0]);

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--daemon"))
			continue;

		if (!args[0])
			args_len += snprintf(args + args_len, sizeof(args) - args_len, " ");

		args_len += snprintf(args + args_len, sizeof(args) - args_len, "\"%s\"", argv[i]);
	}

	STARTUPINFO startInfo = {0};
	PROCESS_INFORMATION process = {0};

	startInfo.cb = sizeof(startInfo);
	startInfo.dwFlags = STARTF_USESHOWWINDOW;
	startInfo.wShowWindow = SW_HIDE;

	if (!CreateProcessA(
		(LPSTR)cmd,              // application name
		(LPSTR)args,             // command line arguments
		NULL,                    // process attributes
		NULL,                    // thread attributes
		FALSE,                   // inherit (file) handles
		DETACHED_PROCESS,        // Detach
		NULL,                    // environment
		(path[0] ? path : NULL), // current directory
		&startInfo,              // startup info
		&process)                // process information
		) {
		fprintf(stderr, "Error: creation of the process failed\n");
		return 1;
	}

	return 0;
#else
	pid_t pid;

	if ((pid = fork()) < 0) // Error
		return -1;
	else if (pid != 0) // Parent
		return 0;

	if (watchdog)
		signal(SIGCHLD, SIG_IGN);

	while (watchdog) {
		pid_t pid;

		if ((pid = fork()) < 0) // Error
			return -1;
		else if (pid != 0) { // Parent
			if (watchdog) {
				int status;
				wait(&status);
				msleep(1000);
			}
			else
				return 0;
		}
		else // Child
			break;
	}

	if (path[0])
		if (chdir(path) < 0)
			fprintf(stderr, "Error: can't chdir(%s)\n", path);

	setsid();
	umask(0);
	close(2);
	close(1);
	close(0);
	return 1;
#endif
}
#endif

extern char **g_envp;

extern int g_index_check;
extern int g_no_jit_index;
extern unsigned long g_index_check_lookups, g_index_check_bad;

static void index_check_report(void)
{
	// Nothing verified and nothing wrong: stay silent. A sweep runs
	// thousands of short processes that never touch an index, and some
	// tests fold stderr into their golden output.

	if (!g_index_check || (!g_index_check_lookups && !g_index_check_bad))
		return;

	fflush(stdout);
	fprintf(stderr, "index-check: %lu indexed lookup%s verified, %lu mismatch%s\n",
		g_index_check_lookups, g_index_check_lookups == 1 ? "" : "s",
		g_index_check_bad, g_index_check_bad == 1 ? "" : "es");
}

#ifdef __wasi__
int main(int ac, char *av[])
{
	char **envp = NULL;
#else
int main(int ac, char *av[], char * envp[])
{
#endif

	srand(time(NULL));
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");
	g_envp = envp;
	const char *homedir;
	g_argv0 = av[0];
	g_argv = av;
	g_argvc = ac;

	if ((homedir = getenv("HOME")) == NULL)
		homedir = ".";

	char histfile[1024];
	snprintf(histfile, sizeof(histfile), "%s/%s", homedir, ".tpl_history");
	//bool did_load = false;
	int i, do_goal = 0, do_lib = 0;
	int version = 0, daemon = 0;
	bool no_res = false, quiet = false;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "--library")) {
			if (++i < ac) {
				g_tpl_lib = TPL_strdup(av[i]);
			}
		}
	}

	prolog *pl = pl_create();

	if (!pl) {
		fprintf(stderr, "Failed to create the prolog system: %s\n", strerror(errno));
		return 1;
	}

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "--")) {
			g_ac = ac;
			g_avc = ++i;
			g_av = av;
			break;
		}

		if (!strcmp(av[i], "-h") || !strcmp(av[i], "--help")) {
			version = 2;
		} else if (!strcmp(av[i], "-v") || !strcmp(av[i], "--version")) {
			version = 1;
		} else if (!strcmp(av[i], "-q") || !strcmp(av[i], "--quiet")) {
			quiet = true;
			set_quiet(pl);
		} else if (!strcmp(av[i], "--index-check")) {
			g_index_check = 1;
			atexit(index_check_report);
		} else if (!strcmp(av[i], "--nojitindex")) {
			g_no_jit_index = 1;
		} else if (!strcmp(av[i], "--nolimit")) {
			set_limit(pl, 0);
		} else if (!strcmp(av[i], "-O0") || !strcmp(av[i], "--noopt")) {
			set_opt(pl, 0);
		} else if (!strcmp(av[i], "-t") || !strcmp(av[i], "--trace")) {
			set_trace(pl);
		} else if (!strcmp(av[i], "-d") || !strcmp(av[i], "--daemon")) {
			daemon = 1;
		} else if (!strcmp(av[i], "--autofail")) {
			set_autofail(pl);
		} else if (!strcmp(av[i], "-f")) {
			no_res = true;
		}
	}

#ifndef __wasi__
	if (daemon) {
		if (!daemonize(ac, av)) {
			pl_destroy(pl);
			return 0;
		}
	} else {
#ifndef _WIN32
		struct sigaction sa;
		sa.sa_handler = &g_sigfn;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGINT, &sa, NULL);
#else
		signal(SIGINT, &g_sigfn);
#endif
	}
#endif

#if !defined(_WIN32) && !defined(__wasi__)
	signal(SIGPIPE, SIG_IGN);
#endif

	for (library *lib = g_libs; lib->name; lib++) {
		if (!strcmp(lib->name, "main")) {
			no_res = true;
			do_lib = do_goal = 0;
			size_t len = *lib->len;
			char *src = malloc(len+1);
			check_error(src, pl_destroy(pl));
			memcpy(src, lib->start, len);
			src[len] = '\0';
			SB(s1);
			SB_sprintf(s1, "library/%s", lib->name);
			module *m = load_text(pl->user_m, src, SB_cstr(s1));
			m->prebuilt = true;
			SB_free(s1);
			free(src);
			check_error(m, pl_destroy(pl));
		}
	}

	if (!no_res && !version)
		pl_consult(pl, "~/.tplrc");

	// Every -g/-e/--goal used to overwrite the last one, so only the
	// FINAL flag on the command line ever ran - documented ("only used
	// once") rather than fixed. Accumulate instead, and run each in
	// the order given, in the same position (after every file on the
	// command line has been consulted) that the single goal ran in
	// before. 16 is far more than any real invocation needs; past that
	// an extra -g is more likely a mistake than a real request, so it
	// is reported rather than silently dropped.

#define MAX_GOALS 16
	const char *goals[MAX_GOALS];
	int n_goals = 0;

	for (i = 1; i < ac; i++) {
		if (!strcmp(av[i], "--"))
			break;

#if 0
		if ((av[i][0] == '-') && did_load) {
			fflush(stdout);
			fprintf(stderr, "Error: options entered after files\n");
			pl_destroy(pl);
			return 0;
		}
#endif

		if (!strcmp(av[i], "--consult")) {
#ifdef _WIN32
			if (!pl_consult_fp(pl, stdin, ".\\")) {
#else
			if (!pl_consult_fp(pl, stdin, "./")) {
#endif
				pl_destroy(pl);
				return 1;
			}
		} else if (!strcmp(av[i], "--library")) {
			do_goal = 0;
			do_lib = 1;
		} else if (!strcmp(av[i], "-f") || !strcmp(av[i], "-l") || !strcmp(av[i], "--file") || !strcmp(av[i], "--consult-file")) {
			if (!strcmp(av[i], "-f"))
				no_res = true;

			do_lib = do_goal = 0;
		} else if (!strcmp(av[i], "-g") || !strcmp(av[i], "-e") || !strcmp(av[i], "--goal") || !strcmp(av[i], "--query-goal")) {
			do_lib = 0;
			do_goal = 1;
		} else if (av[i][0] == '-') {
			continue;
		} else if (do_lib) {
			g_tpl_lib = TPL_strdup(av[i]);
			do_lib = 0;
		} else if (do_goal) {
			do_goal = 0;

			if (n_goals >= MAX_GOALS) {
				fflush(stdout);
				fprintf(stderr, "Error: too many -g goals (max %d)\n", MAX_GOALS);
				pl_destroy(pl);
				return 1;
			}

			goals[n_goals++] = av[i];
		} else {
			if (!pl_consult(pl, av[i])) {
				fflush(stdout);
				fprintf(stderr, "Error: error(existence_error(source_sink,'%s'),consult/1)\n", av[i]);
				pl_destroy(pl);
				return 1;
			}
		}
	}

	for (i = 0; i < n_goals; i++) {
		if (!pl_eval(pl, goals[i], false)) {
			int halt_code = get_halt_code(pl);
			pl_destroy(pl);
			return halt_code;
		}
	}

	if (get_halt(pl)) {
		int halt_code = get_halt_code(pl);
		pl_destroy(pl);
		return halt_code;
	}

	if (version || (isatty(0) && !quiet))
		printf("Trealla Prolog (c) Infradig 2020, %s\n", g_version);

	if (version == 2) {
		fprintf(stdout, "Usage:\n");
		fprintf(stdout, "  tpl [options] [files] [-- args]\n");
		fprintf(stdout, "Options:\n");
		fprintf(stdout, "  -f\t\t\t- ~/.tplrc not loaded\n");
		fprintf(stdout, "  -l file\t\t- load file\n");
		fprintf(stdout, "  file\t\t\t- load file\n");
		fprintf(stdout, "  -g goal\t\t- query goal (may be repeated, up to %d, run in order)\n", MAX_GOALS);
		fprintf(stdout, "  --library path\t- alt to TPL_LIBRARY_PATH env variable\n");
		fprintf(stdout, "  -v, --version\t\t- print version info and exit\n");
		fprintf(stdout, "  -h, --help\t\t- print help info and exit\n");
		fprintf(stdout, "  -O0, --noopt\t\t- turn off optimization\n");
		fprintf(stdout, "  -q, --quiet\t\t- quiet mode\n");
		fprintf(stdout, "  -t, --trace\t\t- trace mode\n");
		fprintf(stdout, "  -d, --daemon\t\t- daemonize\n");
		fprintf(stdout, "  -w, --watchdog\t- create watchdog\n");
		fprintf(stdout, "  --autofail\t\t- autofail queries\n");
		fprintf(stdout, "  --consult\t\t- consult from STDIN\n");
		fprintf(stdout, "  --nolimit\t\t- no memory limit\n");
		fprintf(stdout, "  --nojitindex\t\t- build clause indexes on assert, not on first lookup\n");
		fprintf(stdout, "  --index-check\t\t- verify indexed lookups against a linear scan (debug, slow)\n");
	}

	if (version) {
		pl_destroy(pl);
		return 0;
	}

	if (isatty(0))
		history_load(histfile);

	for (;;) {
		char *line = NULL;

		if (pl_isatty(pl)) {
			if ((line = history_readline_eol(pl, "?- ", '.')) == NULL)
				break;
		} else {
			size_t n = 0;

			if (tpl_getline_fp(&line, &n, pl_stdin(pl)) < 0) {
				TPL_free(line);
				break;
			}

			size_t len = strlen(line);

			while (len && iswspace(line[len-1]))
				len--;

			if (len && (line[len-1] != '.')) {
				// stdout is block-buffered when it is not a tty and stderr
				// is not buffered at all, so this would otherwise appear
				// above the output it comes after.

				fflush(stdout);
				fprintf(stderr, "Error: error(syntax_error(unterminated),read_term/3)\n");
				pl_destroy(pl);
				return 1;
			}
		}

		const char *src = line;

		while (isspace(*src))
			src++;

		if (!*src || (*src == '\n')) {
			TPL_free(line);
			continue;
		}

		g_tpl_interrupt = 0;

#if 1
		pl_eval(pl, src, true);
#else
		pl_sub_query *subq;
		pl_query(pl, src, &subq, 0);

		do {
		} while (pl_redo(subq));
#endif

		TPL_free(line);

		if (get_halt(pl))
			break;

		if (!did_dump_vars(pl) && !get_error(pl)) {
			if (get_redo(pl))
				printf(" ");
			else
				printf("   ");

			printf("%s.\n", get_status(pl) ? "true" : "false");
		}
	}

	if (isatty(0))
		history_save();

	int halt_code = get_halt_code(pl);
	pl_destroy(pl);

	return halt_code;
}
