#include <ether.hpp>
#include <linker.hpp>
#include <stmt.hpp>
#include <expr.hpp>
#include <data_type.hpp>
#include <token.hpp>

#define error_expr(e, fmt, ...) error_expr(this, e, fmt, ##__VA_ARGS__)
#define error_data_type(d, fmt, ...) error_data_type(this, d, fmt, ##__VA_ARGS__)
#define error_token(t, fmt, ...) error_token(this, t, fmt, ##__VA_ARGS__)

#define warning_expr(e, fmt, ...) warning_expr(this, e, fmt, ##__VA_ARGS__)
#define warning_data_type(d, fmt, ...) warning_data_type(this, d, fmt, ##__VA_ARGS__)
#define warning_token(t, fmt, ...) warning_token(this, t, fmt, ##__VA_ARGS__)

#define CHANGE_SCOPE(name)						\
	Scope* name = new Scope;					\
	scope->parent_scope = current_scope;		\
	scope->variables = null;					\
	current_scope = name;						\

#define REVERT_SCOPE(name)						\
	current_scope = name->parent_scope;

error_code Linker::link(Stmt** _stmts) {
	stmts = _stmts;
	
	defined_structs = null;
	defined_functions = null;
	global_scope = new Scope;
	global_scope->parent_scope = null;
	global_scope->variables = null;
	current_scope = global_scope;
	function_in = null;
	error_count = 0;

	add_structs();
	add_functions();
	add_variables();

	check_stmts();

	return (error_count == 0 ?
			ETHER_SUCCESS :
			ETHER_ERROR);
}

void Linker::add_structs() {
	buf_loop(stmts, s) {
		if (stmts[s]->type == S_STRUCT) {
			add_struct(stmts[s]);
		}
	}
}

void Linker::add_struct(Stmt* stmt) {
	buf_loop(defined_structs, i) {
		for (u64 bt = 0; bt < BUILT_IN_TYPES_LEN; bt++) {
			if (str_intern(stmt->struct_stmt.identifier->lexeme) ==
				str_intern(built_in_types[bt])) {
				error_token(stmt->struct_stmt.identifier,
							"redeclaration of built-in type ‘%s’ as a struct;",
							built_in_types[bt]);
				return;
			}
		}
		
		if (is_token_equal(stmt->struct_stmt.identifier,
						   defined_structs[i]->stmt->struct_stmt.identifier)) {
			error_token(stmt->struct_stmt.identifier,
						"redeclaration of struct ‘%s’;",
						stmt->struct_stmt.identifier->lexeme);
			return;
		}
	}

	StructFunctionMap* map = new StructFunctionMap;
	map->stmt = stmt;
	map->functions = null;
	buf_push(defined_structs, map);
}

void Linker::add_functions() {
	buf_loop(stmts, s) {
		if (stmts[s]->type == S_FUNC_DECL) {
			if (!stmts[s]->func_decl.struct_in) {
				add_function_global(stmts[s]);
			}
			else {
				add_function_struct(stmts[s]);
			}
		}
	}
}

void Linker::add_function_global(Stmt* stmt) {
	buf_loop(defined_functions, i) {
		if (is_token_equal(stmt->func_decl.identifier,
						   defined_functions[i]->func_decl.identifier)) {
			error_token(stmt->func_decl.identifier,
						"redefinition of function ‘%s’;",
						stmt->func_decl.identifier->lexeme);
			return;
		}
	}

	buf_push(defined_functions, stmt);
}

void Linker::add_function_struct(Stmt* stmt) {
	u64 idx = 0;
	buf_loop(defined_structs, i) {
		if (stmt->func_decl.struct_in ==
			defined_structs[i]->stmt) {
			idx = i;
			break;
		}
	}

	buf_loop(defined_structs[idx]->functions, f) {
		if (is_token_equal(stmt->func_decl.identifier,
						   defined_structs[idx]->functions[f]->func_decl.identifier)) {
			error_token(stmt->func_decl.identifier,
						"redefinition of struct function ‘%s’;",
						stmt->func_decl.identifier->lexeme);
			return;
		}
	}

	buf_push(defined_structs[idx]->functions, stmt);
}

void Linker::add_variables() {
	buf_loop(stmts, s) {
		if (stmts[s]->type == S_VAR_DECL) {
			add_variable(stmts[s]);
		}
	}
}

void Linker::add_variable(Stmt* stmt) {
	buf_loop(global_scope->variables, i) {
		if (is_token_equal(stmt->var_decl.identifier,
						   global_scope->variables[i]->var_decl.identifier)) {
			error_token(stmt->var_decl.identifier,
					  "redeclaration of variable ‘%s’;",
					  stmt->var_decl.identifier->lexeme);
			return;
		}
	}
	if (stmt->var_decl.initializer) {
		check_expr(stmt->var_decl.initializer);
	}

	buf_push(global_scope->variables, stmt);
}

void Linker::check_stmts() {
	buf_loop(stmts, s) {
		check_stmt(stmts[s]);
	}
}

void Linker::check_stmt(Stmt* stmt) {
	switch (stmt->type) {
	case S_FUNC_DECL:
		check_func_decl(stmt);
		break;
	case S_VAR_DECL:
		if (function_in) { // global vars have already been checked
			check_var_decl(stmt);
		}
		break;
	case S_EXPR_STMT:
		check_expr_stmt(stmt);
		break;
	}
}

void Linker::check_func_decl(Stmt* stmt) {
	CHANGE_SCOPE(scope);
	function_in = stmt;
	if (!stmt->func_decl.struct_in && stmt->func_decl.is_function) {
		buf_loop(stmt->func_decl.params, p) {
			Stmt** params = stmt->func_decl.params;
			VariableScope scope_in_found = is_variable_in_scope(params[p]);
			if (scope_in_found == VS_CURRENT_SCOPE) {
				error_token(params[p]->var_decl.identifier,
							"redeclaration of variable ‘%s’;",
							params[p]->var_decl.identifier->lexeme);
			}
			else if (scope_in_found == VS_OUTER_SCOPE) {
				warning_token(params[p]->var_decl.identifier,
							  "variable declaration shadows another variable;");
				buf_push(scope->variables, params[p]);
			}
			buf_push(scope->variables, params[p]);
		}
		if (stmt->func_decl.return_data_type) {
			check_data_type(stmt->func_decl.return_data_type);
		}

		buf_loop(stmt->func_decl.body, s) {
			check_stmt(stmt->func_decl.body[s]);
		}
	}
	function_in = null;
	REVERT_SCOPE(scope);
}

void Linker::check_var_decl(Stmt* stmt) {
	if (stmt->var_decl.is_variable) {
		VariableScope scope_in_found = is_variable_in_scope(stmt);
		if (scope_in_found == VS_CURRENT_SCOPE) {
			error_token(stmt->var_decl.identifier,
						"redeclaration of variable ‘%s’;",
						stmt->var_decl.identifier->lexeme);
		}
		else if (scope_in_found == VS_OUTER_SCOPE) {
			warning_token(stmt->var_decl.identifier,
						  "variable declaration shadows another variable;");
			buf_push(current_scope->variables, stmt);
		}
		buf_push(current_scope->variables, stmt);

		if (stmt->var_decl.data_type) {
			check_data_type(stmt->var_decl.data_type);
		}

		if (stmt->var_decl.initializer) {
			check_expr(stmt->var_decl.initializer);
		}
	}	
}

void Linker::check_expr_stmt(Stmt* stmt) {
	check_expr(stmt->expr_stmt);
}

void Linker::check_expr(Expr* expr) {
	switch (expr->type) {
	case E_BINARY:
		check_binary_expr(expr);
		break;
	case E_UNARY:
		check_unary_expr(expr);
		break;
	case E_CAST:
		check_cast_expr(expr);
		break;
	case E_FUNC_CALL:
		check_func_call(expr);
		break;
	case E_ARRAY_ACCESS:
		check_array_access(expr);
		break;
	case E_MEMBER_ACCESS:
		check_member_access(expr);
		break;
	case E_VARIABLE_REF:
		check_variable_ref(expr);
		break;
	case E_NUMBER:
	case E_STRING:
	case E_CHAR:
	case E_CONSTANT:
		break;	
	}
}

void Linker::check_binary_expr(Expr* expr) {
	check_expr(expr->binary.left);
	check_expr(expr->binary.right);
}

void Linker::check_unary_expr(Expr* expr) {
	check_expr(expr->unary.right);
}

void Linker::check_cast_expr(Expr* expr) {
	check_data_type(expr->cast.cast_to);
	check_expr(expr->cast.right);
}

void Linker::check_func_call(Expr* expr) {
	// TODO implement
}

void Linker::check_array_access(Expr* expr) {
	check_expr(expr->array_access.left);
	check_expr(expr->array_access.index);
}

void Linker::check_member_access(Expr* expr) {
	check_expr(expr->member_access.left);
	// TODO struct member linkq
	//check_expr(expr->member_access.right);
}

void Linker::check_variable_ref(Expr* expr) {
	VariableScope scope_in_found = is_variable_ref_in_scope(expr);
	if (scope_in_found == VS_NO_SCOPE) {
		error_expr(expr,
				   "undefined variable ‘%s’;",
				   expr->variable_ref.identifier->lexeme);
	}
}

void Linker::check_data_type(DataType* data_type) {
	bool found = false;
	buf_loop(defined_structs, i) {
		if (is_token_equal(defined_structs[i]->stmt->struct_stmt.identifier,
						   data_type->identifier)) {
			found = true;
			break;
		}
	}

	if (!found) {
		for (u64 i = 0; i < BUILT_IN_TYPES_LEN; ++i) {
			if (str_intern(built_in_types[i]) ==
				str_intern(data_type->identifier->lexeme)) {
				found = true;
				break;
			}
		}
	}
	
	if (!found) {
		error_token(data_type->identifier,
					"undefined type ‘%s’;",
					data_type->identifier->lexeme);
	}
}

VariableScope Linker::is_variable_ref_in_scope(Expr* expr) {
	VariableScope scope_in_found = VS_NO_SCOPE;
	bool first_iter = true;
	Scope* scope = current_scope;
	
	while (scope != null) {
		buf_loop(scope->variables, v) {
			if (is_token_equal(expr->variable_ref.identifier,
							   scope->variables[v]->var_decl.identifier)) {
				if (first_iter) {
					scope_in_found = VS_CURRENT_SCOPE;										
				}
				else {
					scope_in_found = VS_OUTER_SCOPE;
				}
				expr->variable_ref.variable_refed = scope->variables[v];
				break;
			}
		}
		
		if (scope_in_found != VS_NO_SCOPE) break;
		scope = scope->parent_scope;
		first_iter = false;
	}
	return scope_in_found;
}

VariableScope Linker::is_variable_in_scope(Stmt* stmt) {
	VariableScope scope_in_found = VS_NO_SCOPE;
	bool first_iter = true;
	Scope* scope = current_scope;

	while (scope != null) {
		buf_loop(scope->variables, v) {
			if (is_token_equal(stmt->var_decl.identifier,
							   scope->variables[v]->var_decl.identifier)) {
				if (first_iter) {
					scope_in_found = VS_CURRENT_SCOPE;
				}
				else {
					scope_in_found = VS_OUTER_SCOPE;
				}
				break;
			}
		}

		if (scope_in_found != VS_NO_SCOPE) break;
		scope = scope->parent_scope;
		first_iter = false;
	}
	return scope_in_found;
}

void Linker::error_root(SourceFile* srcfile, u64 line, u64 column, u64 char_count, const char* fmt, va_list ap) {
	print_error_at(
		srcfile,
		line,
		column,
		char_count,
		fmt,
		ap);
	error_count++;
}

void Linker::warning_root(SourceFile* srcfile, u64 line, u64 column, u64 char_count, const char* fmt, va_list ap) {
	print_warning_at(
		srcfile,
		line,
		column,
		char_count,
		fmt,
		ap);
}
