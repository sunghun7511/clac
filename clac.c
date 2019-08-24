/*
 * Copyright (c) 2017, Michel Martens <mail at soveran dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include "linenoise.h"
#include "sds.h"

/* UI */  
#define HINT_COLOR 33
#define OUTPUT_FMT "\x1b[33m= %d\x1b[0m\n"
#define WORDEF_FMT "%s \x1b[33m\"%s\"\x1b[0m\n"

/* Config */  
#define BUFFER_MAX 1024
#define WORDS_FILE "clac/words"
#define CAPACITY   0xFF

/* Stack */
#define count(S)   ((S)->top)
#define clear(S)   ((S)->top = 0)
#define isfull(S)  ((S)->top == CAPACITY)
#define isempty(S) ((S)->top == 0)

/* Arithmetic */
#define modulo(A, B) (A - B * floor(A / B))

typedef struct stack {
	int top;
	int items[CAPACITY];
} stack;

typedef struct node {
	sds word;
	sds meaning;
	struct node *next;
} node;

typedef struct hcamp_context {
	stack stacks[2];
	stack *s0;
	stack *s1;
	node *head;
	sds result;
	int hole;
} hcamp_context;

typedef hcamp_context* p_context;

p_context mc;

static void init_hcamp_context(p_context context) {
	context->s0 = &context->stacks[0];
	context->s1 = &context->stacks[1];
	context->result = sdsempty();
	context->head = NULL;
	context->hole = 0;
}

static int isoverflow(stack *s) {
	if (isfull(s)) {
		fprintf(stderr, "\r\nStack is full!\n");
		return 1;
	}

	return 0;
}

static int count_s0() {
	return count(mc->s0);
}

static int count_s1() {
	return count(mc->s1);
}

static void clear_s0() {
	clear(mc->s0);
}

static void clear_s1() {
	clear(mc->s1);
}

static int isfull_s0() {
	return isfull(mc->s0);
}

static int isfull_s1() {
	return isfull(mc->s1);
}

static int isempty_s0() {
	return isempty(mc->s0);
}

static int isempty_s1() {
	return isempty(mc->s1);
}

static int __peek(stack *s) {
	return s->items[s->top-1];
}

static int peek_s0() {
	if (isempty(mc->s0)) {
		return 0;
	}
	return __peek(mc->s0);
}

static int peek_s1() {
	if (isempty(mc->s1)) {
		return 0;
	}
	return __peek(mc->s1);
}

static void __push(stack *s, int value) {
	s->items[s->top++] = value;
}

static void push_s0(int value) {
	if (isoverflow(mc->s0)) {
		return;
	}
	return __push(mc->s0, value);
}

static void push_s1(int value) {
	if (isoverflow(mc->s1)) {
		return;
	}
	return __push(mc->s1, value);
}

static int __pop(stack *s) {
	return s->items[--s->top];
}

static int pop_s0() {
	if (isempty(mc->s0)) {
		return 0;
	}
	return __pop(mc->s0);
}

static int pop_s1() {
	if (isempty(mc->s1)) {
		return 0;
	}
	return __pop(mc->s1);
}

static void swap(stack *s) {
	int a = __pop(s);
	int b = __pop(s);

	__push(s, a);
	__push(s, b);
}

static void move(stack *s, stack *t, int n) {
	while (!isempty(s) && n > 0) {
		__push(t, __pop(s));
		n--;
	}
}

static void roll(stack *s, stack *aux, int m, int n) {
	if (m > count(s)) {
		m = count(s);
	}

	if (m < 2) {
		return;
	}

	if (n < 0) {
		n = m - modulo(abs(n), m);
	}

	if (n == 0 || n == m) {
		return;
	}

	m--;

	int a;

	while (n > 0) {
		a = __pop(s);
		move(s, aux, m);
		__push(s, a);
		move(aux, s, m);
		n--;
	}
}

static int __add(stack *s, int n) {
	int a = __pop(s);

	while (!isempty(s) && n > 1) {
		a += __pop(s);
		n--;
	}

	return a;
}

static int add_s0(int n) {
	return __add(mc->s0, n);
}

static int add_s1(int n) {
	return __add(mc->s1, n);
}

static node *get(p_context pc, sds word) {
	node *curr = pc->head;

	while (curr != NULL) {
		if (!strcasecmp(word, curr->word)) {
			return curr;
		}

		curr = curr->next;
	}

	return NULL;
}

static void set(p_context pc, sds word, sds meaning) {
	node *curr = get(pc, word);

	if (curr != NULL) {
		fprintf(stderr, "Duplicate definition of \"%s\"\n", word);
		curr->meaning = meaning;
		return;
	}

	curr = (node *) malloc(sizeof(node));

	if (curr == NULL) {
		fprintf(stderr, "Not enough memory to load words\n");
		exit(1);
	}

	curr->word = word;
	curr->meaning = meaning;
	curr->next = pc->head;
	pc->head = curr;
}

static void cleanup(p_context pc) {
	node *curr;

	while (pc->head != NULL) {
		curr = pc->head;
		pc->head = curr->next;

		sdsfree(curr->word);
		sdsfree(curr->meaning);

		free(curr);
	}
}

static int parse(p_context pc, sds input) {
	int argc;
	sds *argv = sdssplitargs(input, &argc);

	if (argc == 0) {
		sdsfreesplitres(argv, argc);
		return 0;
	}

	if (argc != 2) {
		sdsfreesplitres(argv, argc);
		fprintf(stderr, "Incorrect definition: %s\n", input);
		return 1;
	}

	set(pc, argv[0], argv[1]);

	return 0;
}

static void load(p_context pc, sds filename) {
	FILE *fp;

	if ((fp = fopen(filename, "r")) == NULL) {
		if (errno == ENOENT) {
			return;
		}

		fprintf(stderr, "Can't open file %s\n", filename);
		sdsfree(filename);
		exit(1);
	}

	char buf[BUFFER_MAX+1];
	int linecount, i;

	sds *lines;
	sds content = sdsempty();

	while(fgets(buf, BUFFER_MAX+1, fp) != NULL) {
		content = sdscat(content, buf);
	}

	fclose(fp);

	lines = sdssplitlen(content, strlen(content), "\n", 1, &linecount);

	for (i = 0; i < linecount; i++) {
		lines[i] = sdstrim(lines[i], " \t\r\n");

		if (parse(pc, lines[i]) != 0) {
			sdsfreesplitres(lines, linecount);

			fprintf(stderr, "(%s:%d)\n", filename, i+1);
			exit(1);
		}
	}

	sdsfreesplitres(lines, linecount);
	sdsfree(content);
}

static void eval(p_context pc, const char *input);

static void process(p_context pc, sds word) {
	int a, b;
	char *z;
	node *n;

	if (!strcmp(word, "_")) {
		push_s0(pc->hole);
	} else if (!strcmp(word, "+")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(a + b);
		}
	} else if (!strcmp(word, "-")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(b - a);
		}
	} else if (!strcmp(word, "*")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(b * a);
		}
	} else if (!strcmp(word, "/")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(b / a);
		}
	} else if (!strcmp(word, "%")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(modulo(b, a));
		}
	} else if (!strcmp(word, "^")) {
		if (count_s0() > 1) {
			a = pop_s0();
			b = pop_s0();
			push_s0(pow(b, a));
		}
	} else if (!strcasecmp(word, "sum")) {
		push_s0(add_s0(count_s0()));
	} else if (!strcasecmp(word, "add")) {
		push_s0(add_s0(pop_s0()));
	} else if (!strcasecmp(word, "abs")) {
		if (count_s0() > 0) {
			push_s0(fabs(pop_s0()));
		}
	} else if (!strcasecmp(word, "ceil")) {
		if (count_s0() > 0) {
			push_s0(ceil(pop_s0()));
		}
	} else if (!strcasecmp(word, "floor")) {
		if (count_s0() > 0) {
			push_s0(floor(pop_s0()));
		}
	} else if (!strcasecmp(word, "round")) {
		if (count_s0() > 0) {
			push_s0(round(pop_s0()));
		}
	} else if (!strcasecmp(word, "sin")) {
		if (count_s0() > 0) {
			push_s0(sin(pop_s0()));
		}
	} else if (!strcasecmp(word, "cos")) {
		if (count_s0() > 0) {
			push_s0(cos(pop_s0()));
		}
	} else if (!strcasecmp(word, "tan")) {
		if (count_s0() > 0) {
			push_s0(tan(pop_s0()));
		}
	} else if (!strcasecmp(word, "ln")) {
		if (count_s0() > 0) {
			push_s0(log(pop_s0()));
		}
	} else if (!strcasecmp(word, "log")) {
		if (count_s0() > 0) {
			push_s0(log10(pop_s0()));
		}
	} else if (!strcasecmp(word, "!")) {
		if (count_s0() > 0) {
			a = pop_s0();

			push_s0(a * tgamma(a));
		}
	} else if (!strcasecmp(word, "dup")) {
		if (!isempty_s0()) {
			push_s0(peek_s0());
		}
	} else if (!strcasecmp(word, "roll")) {
		a = pop_s0();
		b = pop_s0();

		roll(pc->s0, pc->s1, b, a);
	} else if (!strcasecmp(word, "swap")) {
		swap(pc->s0);
	} else if (!strcasecmp(word, "drop")) {
		pop_s0();
	} else if (!strcasecmp(word, "count")) {
		push_s0(count_s0());
	} else if (!strcasecmp(word, "clear")) {
		clear_s0();
	} else if (!strcasecmp(word, "stash")) {
		move(pc->s0, pc->s1, pop_s0());
	} else if (!strcasecmp(word, "fetch")) {
		move(pc->s1, pc->s0, pop_s0());
	} else if (!strcasecmp(word, ".")) {
		move(pc->s0, pc->s1, 1);
	} else if (!strcasecmp(word, ",")) {
		move(pc->s1, pc->s0, 1);
	} else if (!strcasecmp(word, ":")) {
		move(pc->s0, pc->s1, count_s0());
	} else if (!strcasecmp(word, ";")) {
		move(pc->s1, pc->s0, count_s1());
 	} else if ((n = get(pc, word)) != NULL) {
 		eval(pc, n->meaning);
	} else {
		a = strtod(word, &z);

		if (*z == '\0') {
			push_s0(a);
		} else if (!isalpha(word[0])) {
			push_s0(NAN);
		}
	}
}

static void eval(p_context pc, const char *input) {
	int i, argc;

	sds *argv = sdssplitargs(input, &argc);

	for (i = 0; i < argc; i++) {
		process(pc, argv[i]);
	}

	sdsfreesplitres(argv, argc);
}

static sds buildpath(const char *fmt, const char *dir) {
	return sdscatfmt(sdsempty(), fmt, dir, WORDS_FILE);
}

static void config(p_context pc) {
}

int main(int argc, char **argv) {
	char *line;
	hcamp_context hc;
	p_context pc = &hc;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

	mc = pc;

	init_hcamp_context(pc);
	config(pc);

	if (argc == 2) {
		eval(pc, argv[1]);

		while (count_s0() > 0) {
			printf("%d\n", pop_s0());
		}

		exit(0);
	}

	if (argc > 2) {
		fprintf(stderr, "usage: clac [expression]\n");
		exit(1);
	}

	while((line = linenoise("> ")) != NULL) {
		if (!strcmp(line, "words")) {
			node *curr = pc->head;

			while (curr != NULL) {
				printf(WORDEF_FMT, curr->word, curr->meaning);
				curr = curr->next;
			}
		} else if (!strcmp(line, "reload")) {
			cleanup(pc);
			config(pc);
		} else if (!strcmp(line, "exit")) {
			break;
		} else {
			clear_s0();
			clear_s1();

			eval(pc, line);

			if (!isempty_s0()) {
				pc->hole = peek_s0();
				clear_s0();
				printf(OUTPUT_FMT, pc->hole);
			}
		}

		sdsclear(pc->result);
		free(line);
	}

	sdsfree(pc->result);
	cleanup(pc);

	return 0;
}
