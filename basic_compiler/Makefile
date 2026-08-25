CC = gcc
CFLAGS=-std=c11 -Wall -Wextra -O0 -g
LEX = flex
YACC=bison
TARGET = tinyc

.PHONY: all compile run test clean

all: compile

compile: $(TARGET)

$(TARGET): advcodegen.y advcodegen.l
	$(YACC) -y -d -o y.tab.c advcodegen.y
	$(LEX) -o lex.yy.c advcodegen.l
	$(CC) $(CFLAGS) y.tab.c lex.yy.c -o $(TARGET)

run: $(TARGET)
	./$(TARGET) tests/full.tc

test: $(TARGET)
	bash tests/run_tests.sh

clean:
	rm -f $(TARGET) y.tab.c y.tab.h lex.yy.c
	rm -rf output_*
