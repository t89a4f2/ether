#pragma once

struct Stmt;

struct Compiler {
	Stmt** compile(const char* in_file, const char* out_file);
};

struct FileDecl {
	char* fpath;
	Stmt** decls;
};
