#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

// On Nixes EDITLINE should be available if not defined uses GNU READLINE.
// On WASM and Windows ISOCLINE?

#if !defined(USE_EDITLINE) && !defined(USE_ISOCLINE) && !defined(USE_READLINE)
#define USE_EDITLINE
#endif

#if defined(USE_ISOCLINE) || defined(_WIN32) && defined(__wasi__)
#include "isocline/include/isocline.h"
#endif

#if defined(USE_EDITLINE) && !defined(__wasi__)
#if !defined(_WIN32)
#include <histedit.h>
#endif
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <editline/readline.h>
#endif
#endif

#if defined(USE_READLINE) && !defined(__wasi__)
#include <readline/readline.h>
#include <readline/history.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__)
#include <termios.h>
#endif

#include "internal.h"
#include "history.h"
#include "prolog.h"

// Turning off line buffering and echo only applies to a terminal. When
// input is a pipe or a file there is nothing to turn off, so read it as
// it comes rather than failing: bailing out here handed the caller a -1
// it could not tell apart from end of input, and it had consumed nothing.

int history_getch_fd(int fd)
{
#if !defined(_WIN32) && !defined(__wasi__)
	struct termios oldattr, newattr;
	bool is_tty = !tcgetattr(fd, &oldattr);

	if (is_tty) {
		newattr = oldattr;
		newattr.c_lflag &= ~(ICANON | ECHO);
		is_tty = !tcsetattr(fd, TCSANOW, &newattr);
	}
#endif
	int ch = fgetc_utf8(stdin);
#if !defined(_WIN32) && !defined(__wasi__)
	if (is_tty)
		tcsetattr(fd, TCSANOW, &oldattr);
#endif
	return ch;
}

int history_getch(void)
{
	return history_getch_fd(STDIN_FILENO);
}

static char g_filename[1024];


#if !USE_ISOCLINE && !defined(__wasi__)
char *history_readline_eol(prolog *pl, const char *prompt, char eol)
{
	char *cmd = NULL;
	char *line;

LOOP:

	if ((line = readline(prompt)) == NULL)
		return NULL;

	if (cmd) {
		size_t n = strlen(cmd) + strlen(line);
		cmd = TPL_realloc(cmd, n+1);
		ENSURE(cmd);
		strcat(cmd, line);
	} else {
		cmd = TPL_strdup(line);
	}

	free(line);
	const char *s = cmd;

	for (;;) {
		int ch = get_char_utf8(&s);
		char *end_ptr = cmd + strlen(cmd) - (strlen(cmd) ? 1 : 0);

		char *s2 = strchr(cmd, '%');

		if (s2 && !strchr(cmd, '"') && !strchr(cmd, '\'')) {
			*s2 = '\0';
			end_ptr--;
		}

		while (isspace((unsigned char)*end_ptr)) {
			*end_ptr = '\0';
			end_ptr--;
		}

		if ((ch == 0) && (*end_ptr == eol)) {
			if (strcmp(cmd, "halt.") && strcmp(cmd, "."))
				add_history(cmd);

			break;
		}

		if (ch == 0) {
			cmd = TPL_realloc(cmd, strlen(cmd)+1+1);
			ENSURE(cmd);
			strcat(cmd, "\n");
			prompt = "";
			goto LOOP;
		}
	}

	return cmd;
}

static char *functor_name_generator(const char *text, int state)
{
	static int s_atts_list_index, s_atts_len;
	static int s_bboard_list_index, s_bboard_len;
	static int s_control_list_index, s_control_len;
	static int s_csv_list_index, s_csv_len;
	static int s_database_list_index, s_database_len;
	static int s_evaluable_list_index, s_evaluable_len;
	static int s_ffi_list_index, s_ffi_len;
	static const void *s_ffi_entry;
	static int s_files_list_index, s_files_len;
	static int s_format_list_index, s_format_len;
	static int s_iso_list_index, s_iso_len;
	static int s_os_list_index, s_os_len;
	static int s_other_list_index, s_other_len;
	static int s_posix_list_index, s_posix_len;
	static int s_misc_list_index, s_misc_len;
	static int s_sort_list_index, s_sort_len;
	static int s_streams_list_index, s_streams_len;
	static int s_sregex_list_index, s_sregex_len;
	static int s_tasks_list_index, s_tasks_len;
	static int s_threads_list_index, s_threads_len;
	const char *name;

	if (!text || !*text)
		return NULL;

	if (!state) {
		s_atts_list_index = 0; s_atts_len = strlen(text);
		s_bboard_list_index = 0; s_bboard_len = strlen(text);
		s_control_list_index = 0; s_control_len = strlen(text);
		s_csv_list_index = 0; s_csv_len = strlen(text);
		s_database_list_index = 0; s_database_len = strlen(text);
		s_evaluable_list_index = 0; s_evaluable_len = strlen(text);
		s_ffi_list_index = 0; s_ffi_len = strlen(text); s_ffi_entry = ffi_registered_first();
		s_files_list_index = 0; s_files_len = strlen(text);
		s_format_list_index = 0; s_format_len = strlen(text);
		s_iso_list_index = 0; s_iso_len = strlen(text);
		s_other_list_index = 0; s_other_len = strlen(text);
		s_os_list_index = 0; s_os_len = strlen(text);
		s_posix_list_index = 0; s_posix_len = strlen(text);
		s_misc_list_index = 0; s_misc_len = strlen(text);
		s_sort_list_index = 0; s_sort_len = strlen(text);
		s_streams_list_index = 0; s_streams_len = strlen(text);
		s_sregex_list_index = 0; s_sregex_len = strlen(text);
		s_tasks_list_index = 0; s_tasks_len = strlen(text);
		s_threads_list_index = 0; s_threads_len = strlen(text);
	}

	while ((name = g_atts_bifs[s_atts_list_index].name)) {
		s_atts_list_index++;

		if (strncmp(name, text, s_atts_len) == 0)
			return strdup(name);
	}

	while ((name = g_bboard_bifs[s_bboard_list_index].name)) {
		s_bboard_list_index++;

		if (strncmp(name, text, s_bboard_len) == 0)
			return strdup(name);
	}

	while ((name = g_csv_bifs[s_csv_list_index].name)) {
		s_csv_list_index++;

		if (strncmp(name, text, s_csv_len) == 0)
			return strdup(name);
	}

	while ((name = g_database_bifs[s_database_list_index].name)) {
		s_database_list_index++;

		if (strncmp(name, text, s_database_len) == 0)
			return strdup(name);
	}

	while ((name = g_evaluable_bifs[s_evaluable_list_index].name)) {
		s_evaluable_list_index++;

		if (strncmp(name, text, s_evaluable_len) == 0)
			return strdup(name);
	}

	while ((name = g_ffi_bifs[s_ffi_list_index].name)) {
		s_ffi_list_index++;

		if (strncmp(name, text, s_ffi_len) == 0)
			return strdup(name);
	}

	while ((name = ffi_registered_next(&s_ffi_entry))) {
		if (strncmp(name, text, s_ffi_len) == 0)
			return strdup(name);
	}

	while ((name = g_format_bifs[s_format_list_index].name)) {
		s_format_list_index++;

		if (strncmp(name, text, s_format_len) == 0)
			return strdup(name);
	}

	while ((name = g_iso_bifs[s_iso_list_index].name)) {
		s_iso_list_index++;

		if (strncmp(name, text, s_iso_len) == 0)
			return strdup(name);
	}

	while ((name = g_misc_bifs[s_misc_list_index].name)) {
		s_misc_list_index++;

		if (strncmp(name, text, s_misc_len) == 0)
			return strdup(name);
	}

	while ((name = g_other_bifs[s_other_list_index].name)) {
		s_other_list_index++;

		if (strncmp(name, text, s_other_len) == 0)
			return strdup(name);
	}

	while ((name = g_os_bifs[s_os_list_index].name)) {
		s_os_list_index++;

		if (strncmp(name, text, s_os_len) == 0)
			return strdup(name);
	}

	while ((name = g_control_bifs[s_control_list_index].name)) {
		s_control_list_index++;

		if (strncmp(name, text, s_control_len) == 0)
			return strdup(name);
	}

	while ((name = g_posix_bifs[s_posix_list_index].name)) {
		s_posix_list_index++;

		if (strncmp(name, text, s_posix_len) == 0)
			return strdup(name);
	}

	while ((name = g_sort_bifs[s_sort_list_index].name)) {
		s_sort_list_index++;

		if (strncmp(name, text, s_sort_len) == 0)
			return strdup(name);
	}

	while ((name = g_sregex_bifs[s_sregex_list_index].name)) {
		s_sregex_list_index++;

		if (strncmp(name, text, s_sregex_len) == 0)
			return strdup(name);
	}

	while ((name = g_streams_bifs[s_streams_list_index].name)) {
		s_streams_list_index++;

		if (strncmp(name, text, s_streams_len) == 0)
			return strdup(name);
	}

	while ((name = g_tasks_bifs[s_tasks_list_index].name)) {
		s_tasks_list_index++;

		if (strncmp(name, text, s_tasks_len) == 0)
			return strdup(name);
	}

	while ((name = g_threads_bifs[s_threads_list_index].name)) {
		s_threads_list_index++;

		if (strncmp(name, text, s_threads_len) == 0)
			return strdup(name);
	}

    return NULL;
}

static char **functor_name_completion(const char *text, int start, int end)
{
	rl_attempted_completion_over = 1;
	rl_completion_append_character = '\0';
	return rl_completion_matches(text, functor_name_generator);
}

void history_load(const char *filename)
{
	snprintf(g_filename, sizeof(g_filename), "%s", filename);
	using_history();
	read_history(g_filename);
	rl_attempted_completion_function = functor_name_completion;
}

void history_save(void)
{
	write_history(g_filename);
	//rl_clear_history();
	clear_history();
}
#endif

#if USE_ISOCLINE && !defined(__wasi__)
char *history_readline_eol(prolog *pl, const char *prompt, char eol)
{
	char *cmd = NULL;
	char *line;

LOOP:

	if ((line = ic_readline(prompt)) == NULL)
		return NULL;

	if (cmd) {
		size_t n = strlen(cmd) + strlen(line);
		cmd = TPL_realloc(cmd, n+1);
		ENSURE(cmd);
		strcat(cmd, line);
	} else {
		cmd = TPL_strdup(line);
	}

	free(line);
	const char *s = cmd;

	for (;;) {
		int ch = get_char_utf8(&s);
		const char *end_ptr = cmd + strlen(cmd) - (strlen(cmd) ? 1 : 0);

		while (isspace((unsigned char)*end_ptr) && (end_ptr != cmd))
			end_ptr--;

		if ((ch == 0) && (*end_ptr == eol)) {
			if (strcmp(cmd, "halt.") && strcmp(cmd, "."))
				ic_history_add(cmd);

			break;
		}

		if (ch == 0) {
			cmd = TPL_realloc(cmd, strlen(cmd)+1+1);
			ENSURE(cmd);
			strcat(cmd, "\n");
			prompt = "";
			goto LOOP;
		}
	}

	return cmd;
}

void history_load(const char *filename)
{
	snprintf(g_filename, sizeof(g_filename), "%s", filename);
	ic_set_history(g_filename, 9999);

	ic_enable_brace_matching(false);
	ic_enable_brace_insertion(false);
	ic_enable_completion_preview(false);
	ic_enable_color(false);

	ic_set_default_completer(NULL, NULL);
	ic_set_default_highlighter(NULL, NULL);

	ic_set_prompt_marker("", "");
}

void history_save(void)
{
}
#endif

#ifdef __wasi__
char *history_readline_eol(prolog *pl, const char *prompt, char eol)
{
	fprintf(stdout, "%s", prompt);
	fflush(stdout);

	size_t len = 0;
	char *line = NULL;

	if (getline(&line, &len, stdin) <= 0)
		return NULL;

	return line;
}

void history_load(const char *filename)
{
}

void history_save(void)
{
}
#endif
