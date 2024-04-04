#include "test.h"

/* made this file to investigate a bug in the parser file buffering */

struct parser {
	FILE *file;
	/* circular buffer */
	char buffer[1024];
	Uint32 iRead, iWrite;
	long line;
	int column;
	int c;
};

static void Refresh(struct parser *parser)
{
	Uint32 nWritten;

	/* this check is needed because the following code expects at least
	 * one character to be readable (not EOF)
	 */
	if (feof(parser->file)) {
		return;
	}
	if (parser->iRead > parser->iWrite) {
		nWritten = sizeof(parser->buffer) - parser->iRead +
			parser->iWrite;
	} else {
		nWritten = parser->iWrite - parser->iRead;
	}
	if (nWritten < sizeof(parser->buffer) / 2) {
		if (parser->iRead == 0) {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) - 1 -
					nWritten, parser->file);
			parser->iWrite += n;
		} else if (parser->iRead < parser->iWrite) {
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) -
					parser->iWrite, parser->file);
			if (parser->iWrite + n >= sizeof(parser->buffer)) {
				parser->iWrite = fread(&parser->buffer[0], 1,
						parser->iRead - 1,
						parser->file);
			} else {
				parser->iWrite += n;
			}
		} else {
			/* in this configuration (buffer size = 1024 and reading
			 * when the buffer was half read this part of the code
			 * will never be reached */
			const size_t n = fread(&parser->buffer[parser->iWrite],
					1, sizeof(parser->buffer) - 1 -
					nWritten, parser->file);
			parser->iWrite += n;
		}
	}
}

static int NextChar(struct parser *parser)
{
	Refresh(parser);
	if (parser->iRead == parser->iWrite) {
		parser->c = EOF;
		return EOF;
	}
	parser->c = parser->buffer[parser->iRead++];
	parser->iRead %= sizeof(parser->buffer);
	if (parser->c == '\n') {
		parser->line++;
		parser->column = 0;
	} else {
		parser->column++;
	}
	return parser->c;
}

int main(void)
{
	FILE *fp;
	struct parser parser;

	fp = fopen("tests/prop/test.prop", "r");
	if (fp == NULL) {
		fprintf(stderr, "err\n");
		return 1;
	}
	memset(&parser, 0, sizeof(parser));
	parser.file = fp;
	while (NextChar(&parser) != EOF) {
		fputc(parser.c, stdout);
	}
	fclose(fp);
	return 0;
}
