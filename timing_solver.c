#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h> 
#include <errno.h>
#include <string.h>
#include <limits.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

typedef uint8_t       u8;
typedef unsigned long ulong;

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

void print(const char* s) {
	fputs(s, stdout);
}

#define INIT_ARENA_SIZE 10000 // bytes

typedef struct {
	u8*  memory;
	ulong size;
	ulong used;
} Arena;

Arena make_arena() {
	Arena arena;
	arena.memory = malloc(INIT_ARENA_SIZE);
	arena.size   = INIT_ARENA_SIZE;
	arena.used   = 0;
	return arena;
}

void pop(Arena* arena, ulong n) {
	arena->used -= n;
}

void* end(Arena* arena) {
	return arena->memory + arena->used;
}

void* alloc(Arena* arena, ulong needed) {
	void* result = end(arena);	
	arena->used += needed;

	if (arena->used > arena->size) {
		puts("Out of memory. Considering increasing the amount of memory "
				 "the program uses using the -mem flag.\n");
		exit(EXIT_FAILURE);
	}

	return result;
}

typedef struct {
	char* name;
	ulong size;
	char* text;
} File;

File read_file(char* name) {
	int fd = open(name, O_RDONLY);
	if (fd == -1) {
		printf("Failed to open file '%s': %s\n", name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	struct stat statbuf;
	if (fstat(fd, &statbuf) == -1) {
		printf("Failed to static file '%s': %s\n", name, strerror(errno));
		exit(EXIT_FAILURE);
	}

	off_t size = statbuf.st_size;

	char* text = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (text == (char*) -1) {
		printf("Failed to mmap file '%s': %s\n", name, strerror(errno));
		exit(EXIT_FAILURE);
	}

	File file;
	file.name = name;
	file.size = size;
	file.text = text;
	return file;
}

typedef struct {
	File* file;
	ulong line_n;
	char* line;

	char* start; // start == NULL => this is an End Of File (EOF) token
	ulong len;
} Token;

void print_token(Token token) {
	ulong len   = token.len; 
	char* start = token.start;

	if (start == NULL)
		puts("Token(EOF)");
	else
		printf("Token(\"%.*s\", len = %ld)\n", (int) len, start, len);
}

typedef struct {
	File* file;
	ulong line_n;
	char* line;
	
	ulong idx;
	
	Token stored;
	bool  peeked;
} Scanner;

Scanner make_scanner(File* file) {
	Scanner scanner;
	scanner.file   = file;
	scanner.line_n = 1;
	scanner.line   = file->text;
	scanner.idx    = 0;
	scanner.peeked = false;
	return scanner;
}

bool is_space(char c) {
	return c == ' ' || c == '\n' || c == '\t';
}

bool is_digit(char c) {
	return '0' <= c && c <= '9';
}

bool is_letter(char c) {
	return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

const char* reserved[] = {
	"[", "]", "(", ")", ",",
};

ulong reserved_len = sizeof(reserved) / sizeof(*reserved);

ulong next_reserved(char* text) {
	for (int i = 0; i < reserved_len; i++) {
		const char* s = reserved[i];

		if (strncmp(s, text, strlen(s)) == 0)
			return strlen(s);
	}

	return 0;
}

Token next_token(Scanner* scanner) {
	if (scanner->peeked) {
		scanner->peeked = false;
		return scanner->stored;
	}
	
	File* file   = scanner->file;
	ulong line_n = scanner->line_n;
	char* line   = scanner->line;
	ulong idx    = scanner->idx;

	Token token;
	token.file  = file;
	token.start = NULL;
	
	char* text = file->text;

	if (idx > file->size - 1) 
		return token;

	while (is_space(text[idx])) {
		if (text[idx] == '\n') {
			line_n++;
			line = text + idx + 1; 
		}
		idx++;
	}

	if (idx > file->size - 1) 
		return token;	

	token.line_n = line_n;
	token.line   = line;
	token.start  = text + idx;
	token.len    = 0;
	
	ulong idx0 = idx;
	ulong rlen = next_reserved(text + idx);

	if (is_digit(text[idx]))
		while (idx < file->size && is_digit(text[idx]))
			idx++;
	else if (rlen > 0)
		idx += rlen;
	else if (is_letter(text[idx]))
		while (idx < file->size &&
					 (is_letter(text[idx]) || is_digit(text[idx])))
			idx++;
	else
		while (idx < file->size &&
					 !is_space(text[idx]) &&
					 !is_digit(text[idx]) &&
					 !is_letter(text[idx]) &&
					 !next_reserved(text + idx))
			idx++;

	token.len = idx - idx0;
	
	scanner->line_n = line_n;
	scanner->line   = line;
	scanner->idx    = idx;
	return token;
}

Token peek_token(Scanner* scanner) {
	if (scanner->peeked)
		return scanner->stored;
	
	Token token     = next_token(scanner);
	scanner->stored = token;
	scanner->peeked = true;
	return token;
}

bool eof(Token token) {
	return token.start == NULL;
}

bool is(Token token, const char* kind) {
	if (eof(token))
		return false;
	
	char* start = token.start;
	ulong len   = token.len;
	ulong idx   = 0;

	while (idx < len) {
		if (kind[idx] != start[idx])
			return false;
		idx++;
	}

	return kind[idx] == '\0';
}

bool next_is(Scanner* scanner, const char* kind) {
	Token token = peek_token(scanner);
	return eof(token) || is(token, kind);
}

bool token_eq(Token t0, Token t1) {
	if (t0.len != t1.len)
		return false;

	for (ulong i = 0; i < t0.len; i++)
		if (t0.start[i] != t1.start[i])
			return false;

	return true;
}

ulong remaining_commas(Scanner* scanner) {
	char* text  = scanner->file->text;
	ulong size  = scanner->file->size;
	ulong idx   = scanner->idx;
	ulong count = 0;
	
	for (ulong i = idx; i < size; i++)
		if (text[i] == ',')
			count++;

	return count;
}

void print_tokens(File* file) {
	Scanner scanner = make_scanner(file);
	Token   token   = next_token(&scanner);

	while (!eof(token)) {
		print_token(token);
		token = next_token(&scanner);
	}

	print_token(token);
}

__attribute__((noreturn)) void error(Token token, const char* format, ...) {
	char* line = token.line;
	File* file = token.file;
	ulong col  = token.start - token.line + 1;
	
	printf("%s:%lu:%lu: ", file->name, token.line_n, col);
	
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	while (*line != '\n' && *line != '\0') {
		putchar(*line);
		line++;
	}
	
	putchar('\n');

	for (int i = 0; i < token.start - token.line; i++)
		putchar(is_space(token.line[i]) ? token.line[i] : ' ');

	for (int i = 0; i < token.len; i++)
		putchar('^');
	
	putchar('\n');
	exit(EXIT_FAILURE);
}

void parse_literal(Scanner* scanner, const char* s) {
	Token token = next_token(scanner);

	if (!is(token, s))
		error(token, "Expected '%s', got '%.*s'.\n", s, token.len, token.start);
}

void parse_one_of(Scanner* scanner, const char* s0, const char* s1) {
	Token token = next_token(scanner);
	
	if (is(token, s0))
		return;

	if (!is(token, s1))
		error(token, "Expected one of '%s' or '%s', got '%.*s'.\n", s0, s1,
					token.len, token.start);
}

ulong parse_integer(Scanner* scanner) {
	Token token   = next_token(scanner);
	char* start   = token.start;
	ulong len     = token.len;
	ulong integer = 0;
	
	for (ulong i = 0; i < len; i++) {
		if (!is_digit(start[i]))
			error(token, "Expected integer, got '%.*s'.\n", len, start);

		ulong digit = start[i] - '0';
		
		if ((ULONG_MAX - digit) / 10 < integer)
			error(token, "Integer is too large. Max integer is %ul.\n", ULONG_MAX);
			
		integer *= 10;
		integer += digit;
	}

	return integer;
}

typedef struct {
	char* start;
	ulong len;
	ulong arity;
	ulong delay;
} Gate;

bool gate_equals(Gate gate, Token token) {
	if (gate.len != token.len)
		return false;

	for (ulong i = 0; i < gate.len; i++)
		if (gate.start[i] != token.start[i])
			return false;

	return true;
}

void print_gate(Gate gate) {
	printf("Gate(\"%.*s\", arity = %lu delay = %lu)\n",
				 (int) gate.len, gate.start, gate.arity, gate.delay);
}

Gate parse_gate(Scanner* scanner) {
	Token token = next_token(scanner);

	ulong arity = 2;
	if (is(token, "arity") || is(token, "a"))
		if (!eof(peek_token(scanner)) && is_digit(*peek_token(scanner).start)) {	 
			arity = parse_integer(scanner);
			token = next_token(scanner);
		}

	parse_one_of(scanner, "delays", "d");
								
	ulong delay = parse_integer(scanner);
	
	Gate gate;
	gate.start = token.start;
	gate.len   = token.len;
	gate.arity = arity;
	gate.delay = delay;
	return gate;
}

typedef struct {
	Gate* arr;
	ulong len;
} Gates;

void print_gates(Gates gates) {
	for (ulong i = 0; i < gates.len; i++)
		print_gate(gates.arr[i]);
}

Gates parse_gates(Scanner* scanner, Arena* arena) {
	Gate* arr = end(arena);
	ulong len = 0;

	parse_literal(scanner, "GATES");
	
	while (!next_is(scanner, "CLOCK")) {
		Token token = peek_token(scanner);
		Gate gate   = parse_gate(scanner);
		
		for (ulong i = 0; i < len; i++) {
			if (arr[i].len != gate.len || arr[i].arity != gate.arity)
				continue;

			if (strncmp(arr[i].start, gate.start, gate.len) != 0)
				continue;

			error(token, "Already declared operator \"%.*s\" with arity %lu.\n",
						gate.len, gate.start, gate.arity);
		}
		
		Gate* store = alloc(arena, sizeof(Gate));
		*store      = gate;
		len++;
	}

	Gates gates;
	gates.arr = arr;
	gates.len = len;
	return gates;
}

typedef struct {
	ulong given;
	ulong setup;
	ulong hold;
	ulong c2q; 
	ulong prop; 
} Clock;

void print_clock(Clock clock) {
	printf("Clock(given = %lu, setup = %lu, hold = %lu, c2q = %lu, prop = %lu)"
				 "\n", clock.given, clock.setup, clock.hold, clock.c2q, clock.prop);
}

Clock parse_clock(Scanner* scanner) {
	ulong given                  = 0;
	static const char* names [4] = { "setup", "hold", "c2q", "prop" };
	ulong              fields[4] = {};

	parse_literal(scanner, "CLOCK");

	while (!next_is(scanner, "CIRCUIT")) {
		Token token = next_token(scanner);

		parse_literal(scanner, "=");
		
		ulong n = parse_integer(scanner);

		for (ulong i = 0; i < sizeof(names) / sizeof(*names); i++) {
			if (is(token, names[i])) {
				if ((given >> i) & 1)
					error(token, "Already defined %s above\n", names[i]);
				given |= 1 << i;
				fields[i] = n;
			}
		}
	}

	Clock clock;
	clock.given = given;
	clock.setup = fields[0];
	clock.hold  = fields[1];
	clock.c2q   = fields[2];
	clock.prop  = fields[3];
	return clock;
}

typedef struct Circuit Circuit;

struct Circuit {
	Token*    var;   // NULL => not var
	Gate*     gate;  // NULL => is register
	Circuit*  in;    // NULL => no inputs
	
	bool      seen;
	ulong     min_delay;
	ulong     max_delay;
};

bool is_var(Circuit circuit) {
	return circuit.var != NULL;
}

bool is_reg(Circuit circuit) {
	return !is_var(circuit) && circuit.gate == NULL;
}

void print_formula(Circuit circuit) {
	Token*   var  = circuit.var;
	Gate*    gate = circuit.gate;
	Circuit* in   = circuit.in;

	if (is_var(circuit)) {
		if (circuit.in == NULL)
			printf("%.*s", (int) var->len, var->start);
		else
			print_formula(*circuit.in);
	} else if (is_reg(circuit))
		if (in == NULL)
			print("[]");
		else {
			putchar('[');
			print_formula(*in);
			putchar(']');
		}
	else if (gate->arity == 1) {
		printf("%.*s(", (int) gate->len, gate->start);
		print_formula(*in);
		putchar(')');
	} else if (gate->arity == 2) {
		putchar('(');
		print_formula(in[0]);
		printf(") %.*s (", (int) gate->len, gate->start);
		print_formula(in[1]);
		putchar(')');
	} else {
		printf("%.*s", (int) gate->len, gate->start);
		for (ulong i = 0; i < gate->arity; i++) {
			print(" (");
			print_formula(in[i]);
			putchar(')');
		}
	}

	printf("{%lu}", circuit.max_delay);
}

bool formula_end(Scanner* scanner) {
	return
		eof(peek_token(scanner)) ||
		next_is(scanner, "]") ||
		next_is(scanner, ")");
}

Circuit parse_formula(Scanner* scanner, Arena* arena, Gates gates) {
	Token*    var  = NULL;
	Gate*     gate = NULL;
	Circuit*  in   = NULL;

	if (next_is(scanner, "[")) {
		next_token(scanner);
		if (!next_is(scanner, "]")) { 
			gate = NULL;
			in   = alloc(arena, sizeof(Circuit));
			*in = parse_formula(scanner, arena, gates);
		}
		parse_literal(scanner, "]");
	} else if (next_is(scanner, "(")) {
		next_token(scanner);
		Circuit inner = parse_formula(scanner, arena, gates);
		var  = inner.var;
		gate = inner.gate;
		in   = inner.in;
		parse_literal(scanner, ")");
	} else {
		Token token     = next_token(scanner);
		bool  is_var    = true;
		ulong max_arity = 0;

		for (ulong i = 0; i < gates.len; i++) {
			Gate gate_i = gates.arr[i];
			
			if (!gate_equals(gate_i, token))
				continue;

			ulong arity = gate_i.arity;
			
			if (arity > max_arity) {
				is_var = false;
				max_arity = arity;
			}
		}

		Circuit* args      = alloc(arena, sizeof(Circuit) * max_arity);
		ulong    args_len  = 0;

		while (!formula_end(scanner) && args_len < max_arity) {
			args[args_len] = parse_formula(scanner, arena, gates);
			args_len++;
		}

		for (ulong i = 0; i < gates.len; i++) {
			Gate gate_i = gates.arr[i];
			
			if (!gate_equals(gate_i, token))
				continue;

			ulong arity = gate_i.arity;
			
			if (arity == args_len) {
				gate = gates.arr + i;
				in   = args;
			}
		}

		if (!is_var && gate == NULL)
			error(token, "Invalid number of operands, got %lu.\n", args_len);
		
		if (is_var) {
			var  = alloc(arena, sizeof(Token));
			*var = token;
		}
	}
	
	Circuit circuit;
	circuit.var  = var;
	circuit.gate = gate;
	circuit.in   = in;
	circuit.seen = false;
	return circuit;
}

typedef struct {
	Token   name;
	Circuit formula;
} Var;

void print_var(Var var) {
	printf("Var(%.*s = ", (int) var.name.len, var.name.start);
	print_formula(var.formula);
	puts(")");
}

Var parse_var(Scanner* scanner, Arena* arena, Gates gates) {
	Var var;
	var.name    = next_token(scanner);
	parse_literal(scanner, "is");
	var.formula = parse_formula(scanner, arena, gates);
	return var;
}

typedef struct {
	Var*  arr;
	ulong len;
} Vars;

void print_vars(Vars vars) {
	for (ulong i = 0; i < vars.len; i++)
		print_var(vars.arr[i]);
}

Vars parse_vars(Scanner* scanner, Arena* arena, Gates gates) {
	ulong len = remaining_commas(scanner);
	Var*  arr = alloc(arena, sizeof(Var) * len);

	for (ulong i = 0; i < len; i++) {
		Token token = peek_token(scanner);
		arr[i] = parse_var(scanner, arena, gates);
		parse_literal(scanner, ",");

		for (ulong j = 0; j < i; j++)
			if (token_eq(arr[i].name, arr[j].name))
				error(token, "Redefined variable \"%.*s\".\n", token.len, token.start);
	}

	Vars vars;
	vars.arr = arr;
	vars.len = len;
	return vars;
}

void resolve_circuit(Vars vars, Circuit* circuit) {
	if (is_var(*circuit)) {
		if (circuit->in != NULL)
			return;
	
		for (ulong i = 0; i < vars.len; i++)
			if (token_eq(*circuit->var, vars.arr[i].name)) {
				circuit->in = &vars.arr[i].formula;
				goto FOUND_VAR;
			}

		error(*circuit->var, "Variable not defined.\n");

	FOUND_VAR:
		resolve_circuit(vars, circuit->in);
	}	else if (is_reg(*circuit)) {
		if (circuit->in != NULL)
			resolve_circuit(vars, circuit->in);
	} else
		for (ulong i = 0; i < circuit->gate->arity; i++)
			resolve_circuit(vars, circuit->in + i);
}

Circuit parse_circuit(Scanner* scanner, Arena* arena, Gates gates) {
	parse_literal(scanner, "CIRCUIT");

	Vars vars = parse_vars(scanner, arena, gates);
	print_vars(vars);
	
	Circuit formula = parse_formula(scanner, arena, gates);

	resolve_circuit(vars, &formula);
	return formula;
}

ulong compute_min_delays(Circuit* circuit) {
	ulong min_delay = ULONG_MAX;

	if (is_reg(*circuit)) {
		if (circuit->in)
			min_delay = compute_min_delays(circuit->in);
	} else if (is_var(*circuit))
		min_delay = compute_min_delays(circuit->in);
	else if (circuit->gate) {
		for (ulong i = 0; i < circuit->gate->arity; i++) {
			ulong in_delay = compute_min_delays(circuit->in + i);
			min_delay = MIN(min_delay, in_delay);
		}
		if (min_delay == ULONG_MAX)
			min_delay = 0;
		min_delay += circuit->gate->delay;
	}

	circuit->min_delay = min_delay;
	return min_delay;
}

ulong get_min_delay(Circuit circuit) {
	ulong delay = ULONG_MAX;

	if (is_reg(circuit)) {
		delay = circuit.min_delay;
		if (circuit.in)
			delay = MIN(delay, get_min_delay(*circuit.in));
	} else if (is_var(circuit))
		delay = get_min_delay(*circuit.in);
	else if (circuit.gate)
		for (ulong i = 0; i < circuit.gate->arity; i++) {
			ulong in_delay = get_min_delay(circuit.in[i]);
			delay = MIN(delay, in_delay);
		}

	return delay;
}

ulong compute_max_delays(Circuit* circuit) {
	ulong max_delay = 0;

	if (is_reg(*circuit)) {
		if (circuit->in)
			max_delay = compute_max_delays(circuit->in);
	} else if (is_var(*circuit))
		max_delay = compute_max_delays(circuit->in);
	else if (circuit->gate) {
		for (ulong i = 0; i < circuit->gate->arity; i++) {
			ulong in_delay = compute_max_delays(circuit->in + i);
			max_delay = MAX(max_delay, in_delay);
		}
		max_delay += circuit->gate->delay;
	}

	circuit->max_delay = max_delay;
	return max_delay;
}

ulong get_max_delay(Circuit circuit) {
	ulong delay = 0;

	if (is_reg(circuit)) {
		delay = circuit.max_delay;
		if (circuit.in)
			delay = MAX(delay, get_max_delay(*circuit.in));
	} else if (is_var(circuit))
		delay = get_max_delay(*circuit.in);
	else if (circuit.gate)
		for (ulong i = 0; i < circuit.gate->arity; i++) {
			ulong in_delay = get_max_delay(circuit.in[i]);
			delay = MAX(delay, in_delay);
		}

	return delay;
}

int main(int argc, char** argv) {
	Arena arena     = make_arena();
	File file       = read_file(argv[1]);
	Scanner scanner = make_scanner(&file);

  print_tokens(&file);

	Gates gates = parse_gates(&scanner, &arena);
	print_gates(gates);

	Clock clock = parse_clock(&scanner);
	print_clock(clock);

	Circuit circuit = parse_circuit(&scanner, &arena, gates);
	compute_min_delays(&circuit);
	compute_max_delays(&circuit);
	print_formula(circuit); putchar('\n');

	if ((clock.given >> 2) & 1) {
		ulong c2q       = clock.c2q;
		ulong min_delay = get_min_delay(circuit);

		printf("Shortest path delay: %lu\n", min_delay);
		printf("t_hold <= %lu\n", c2q + min_delay);
	}

	if (clock.given & 1) {
		ulong setup     = clock.setup;
		ulong max_delay = get_max_delay(circuit);

		printf("Longest path delay: %lu\n", max_delay);
		printf("t_period >= %lu\n", setup + max_delay);
	}
}
