/* -*- coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- */

/*
 * Copyright (c) 2025, Awe Morris. All rights reserved.
 */

/*
 * Tag System
 */

#include <playfield/playfield.h>

#include "tag.h"
#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* False assertion */
#define NEVER_COME_HERE		0

/* Sizes. */
#define TAG_NAME_MAX		128
#define PROP_NAME_MAX		128
#define PROP_VALUE_MAX		4096
#define COMMAND_MAX		65536

/* Current tag file. */
static char cur_file[1024];

/* Current tag index. */
static int cur_index;

/* Tag table. */
static struct tag tag[COMMAND_MAX];

/* Tag size. */
static int tag_size;

/* Forward declaration. */
static bool parse_tag_document(const char *doc, bool (*callback)(const char *, int, const char **, const char **, int), char **error_msg, int *error_line);
static bool parse_tag_callback(const char *name, int props, const char **prop_name, const char **prop_value, int line);

/*
 * Initialize the tag system.
 */
void init_tag(void)
{
	cleanup_tag();
}

/*
 * Cleanup the tag system.
 */
void cleanup_tag(void)
{
	struct tag *t;
	int i, j;

	cur_index = 0;

	strcpy(cur_file, "");

	for (i = 0; i < tag_size; i++) {
		t = &tag[i];
		free(t->tag_name);
		for (j = 0; j < PROP_MAX; j++) {
			if (t->prop_name[i] != NULL) {
				free(t->prop_name[i]);
				t->prop_name[i] = NULL;
			}
			if (t->prop_value[i] != NULL) {
				free(t->prop_value[i]);
				t->prop_value[i] = NULL;
			}
		}
	}
}

/*
 * Load a tag file.
 */
bool load_tag_file(const char *file)
{
	char *buf;
	char *error_message;
	int error_line;

	/* Get the file content. */
	if (!load_file(file, &buf, NULL))
		return false;

	/* Destroy the existing commands. */
	cleanup_tag();

	/* Save the file name. */
	strncpy(cur_file, file, sizeof(cur_file) - 1);

	/* Parse the file content. */
	if (!parse_tag_document(buf, parse_tag_callback, &error_message, &error_line)) {
		log_error(S_TR("%s:%d: %s\n"),  file, error_line, error_message);
		free(buf);
		return false;
	}

	/* Free the file content. */
	free(buf);

	return true;
}

/*
 * Get a file name of the current tag.
 */
const char *get_tag_file_name(void)
{
	return &cur_file[0];
}

/*
 * Get a line number of the current tag.
 */
int get_tag_line(void)
{
	/* If the current tag index is invalid. */
	if (cur_index >= tag_size)
		return -1;
	
	return tag[cur_index].line;
}

/*
 * Get a current tag.
 */
struct tag *get_current_tag(void)
{
	/* If the current command index is invalid. */
	if (cur_index >= tag_size)
		return NULL;
	
	return &tag[cur_index];
}

/*
 * Move to the next tag.
 */
void move_to_next_tag(void)
{
	cur_index++;
}

/* Parse a tag document. */
static bool
parse_tag_document(
	const char *doc,
	bool (*callback)(const char *, int, const char **, const char **, int),
	char **error_msg,
	int *error_line)
{
	static char tag_name[TAG_NAME_MAX];
	static char prop_name[PROP_MAX][PROP_NAME_MAX];
	static char prop_val[PROP_MAX][PROP_VALUE_MAX];

	/* State machine */
	enum state {
		ST_INIT,
		ST_TAGNAME,
		ST_PROPNAME,
		ST_PROPVALUE_QUOTE,
		ST_PROPVALUE_BODY,
	};

	const char *top;
	char c;
	int state;
	int line;
	int len;
	int prop_count;
	char *prop_name_tbl[PROP_MAX];
	char *prop_val_tbl[PROP_MAX];
	int i;

	for (i = 0; i < PROP_MAX; i++) {
		prop_name_tbl[i] = &prop_name[i][0];
		prop_val_tbl[i] = &prop_val[i][0];
	}

	state = ST_INIT;
	top = doc;
	line = 1;
	len = 0;
	prop_count = 0;
	while (*top != '\0') {
		c = *top++;
		switch (state) {
		case ST_INIT:
			if (c == '[') {
				state = ST_TAGNAME;
				len = 0;
				continue;
			}
			if (c == '\n') {
				line++;
				continue;
			}
			if (c == ' ' || c == '\r' || c == '\t')
				continue;

			*error_msg = strdup(S_TR("Invalid character."));
			*error_line = line;
			return false;
		case ST_TAGNAME:
			if (len == 0 && (c == ' ' || c == '\r' || c == '\t' || c == '\n'))
				continue;
			if (c == '\n')
				line++;
			if (c == ' ' || c == '\r' || c == '\t' || c == '\n') {
				assert(len > 0);
				tag_name[len] = '\0';
				state = ST_PROPNAME;
				len = 0;
				continue;
			}
			if (c == ']') {
				tag_name[len] = '\0';
				if (!callback(tag_name, 0, NULL, NULL, line)) {
					*error_msg = strdup(S_TR("Too many properties."));
					*error_line = line;
					return false;
				}
				state = ST_INIT;
				continue;
			}
			if (len >= TAG_NAME_MAX) {
				*error_msg = strdup(S_TR("Tag name too long."));
				*error_line = line;
				return false;
			}
			tag_name[len++] = c;
			continue;
		case ST_PROPNAME:
			if (prop_count == PROP_MAX) {
				*error_msg = strdup(S_TR("Too many properties."));
				*error_line = line;
				return false;
			}
			if (len == 0 && c == ' ')
				continue;
			if (len == 0 && c == ']') {
				if (!callback(tag_name, prop_count, (const char **)prop_name_tbl, (const char **)prop_val_tbl, line)) {
					*error_msg = strdup(S_TR("Internal error."));
					*error_line = line;
					return false;
				}
				state = ST_INIT;
				continue;
			}
			if (len == 0 && c == '\n')
				line++;
			if (len == 0 && (c == ' ' || c == '\r' || c == '\t' || c == '\n'))
				continue;
			if (len > 0 && c == '=') {
				assert(len > 0);

				/* Terminate the property name. */
				prop_name[prop_count][len] = '\0';

				state = ST_PROPVALUE_QUOTE;
				len = 0;
				continue;
			}
			if (len >= PROP_NAME_MAX) {
				*error_msg = strdup(S_TR("Property name too long."));
				*error_line = line;
				return false;
			}
			if ((c >= 'a' && c <= 'z') ||
			    (c >= 'A' && c <= 'Z') ||
			    (c >= '0' && c <= '9') ||
			    c == '-' ||
			    c == '_') {
				prop_name[prop_count][len++] = c;
				continue;
			}
			*error_msg = strdup(S_TR("Invalid character."));
			*error_line = line;
			continue;
		case ST_PROPVALUE_QUOTE:
			if (c == '\n')
				line++;
			if (c == ' ' || c == '\r' || c == '\t' || c == '\n')
				continue;
			if (c == '\"') {
				state = ST_PROPVALUE_BODY;
				len = 0;
				continue;
			}
			continue;
		case ST_PROPVALUE_BODY:
			if (c == '\\') {
				switch (*top) {
				case '\"':
					prop_val[prop_count][len] = '\"';
					len++;
					top++;
					continue;
				case 'n':
					prop_val[prop_count][len] = '\n';
					len++;
					top++;
					continue;
				case '\\':
					prop_val[prop_count][len] = '\\';
					len++;
					top++;
					continue;
				default:
					prop_val[prop_count][len] = '\\';
					len++;
					continue;
				}
			}
			if (c == '\"') {
				prop_val[prop_count][len] = '\0';
				prop_count++;

				state = ST_PROPNAME;
				len = 0;
				continue;
			}
			if (len >= PROP_VALUE_MAX) {
				*error_msg = strdup(S_TR("Property value too long."));
				*error_line = line;
				return false;
			}
			prop_val[prop_count][len++] = c;
			continue;
		default:
			assert(NEVER_COME_HERE);
			break;
		}
	}

	if (state == ST_INIT)
		return true;

	*error_msg = strdup(S_TR("Unexpected EOF"));
	*error_line = line;
	return false;
}

/* Callback for when a tag is read. */
static bool parse_tag_callback(const char *name, int props, const char **prop_name, const char **prop_value, int line)
{
	struct tag *t;
	int i;

	/* If command table is full. */
	if (tag_size >= COMMAND_MAX) {
		log_error("Too many tags.");
		return false;
	}

	t = &tag[tag_size++];
	t->prop_count = props;

	/* Copy a tag name. */
	t->tag_name = strdup(name);
	if (t->tag_name == NULL) {
		log_out_of_memory();
		return false;
	}

	/* Copy properties. */
	for (i = 0; i < props; i++) {
		t->prop_name[i] = strdup(prop_name[i]);
		if (t->prop_name[i] == NULL) {
			log_out_of_memory();
			return false;
		}

		t->prop_value[i] = strdup(prop_value[i]);
		if (t->prop_value[i] == NULL) {
			log_out_of_memory();
			return false;
		}
	}

	t->line = line;

	return true;
}
