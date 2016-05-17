#pragma once

#include "syntax_tree.hpp"
#include "d3d10_runtime.hpp"

#include <sstream>

namespace reshade
{
	class d3d10_fx_compiler
	{
	public:
		d3d10_fx_compiler(d3d10_runtime *runtime, const reshadefx::syntax_tree &ast, std::string &errors, bool skipoptimization = false);

		bool run();

	private:
		void error(const reshadefx::location &location, const std::string &message);
		void warning(const reshadefx::location &location, const std::string &message);

		void visit(std::stringstream &output, const reshadefx::nodes::statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::type_node &type, bool with_qualifiers = true);
		void visit(std::stringstream &output, const reshadefx::nodes::lvalue_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::literal_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::expression_sequence_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::unary_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::binary_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::intrinsic_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::conditional_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::swizzle_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::field_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::assignment_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::call_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::constructor_expression_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::initializer_list_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::compound_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::declarator_list_node *node, bool single_statement);
		void visit(std::stringstream &output, const reshadefx::nodes::expression_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::if_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::switch_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::case_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::for_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::while_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::return_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::jump_statement_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::struct_declaration_node *node);
		void visit(std::stringstream &output, const reshadefx::nodes::variable_declaration_node *node, bool with_type = true);
		void visit(std::stringstream &output, const reshadefx::nodes::function_declaration_node *node);

		void visit_texture(const reshadefx::nodes::variable_declaration_node *node);
		void visit_sampler(const reshadefx::nodes::variable_declaration_node *node);
		void visit_uniform(const reshadefx::nodes::variable_declaration_node *node);
		void visit_technique(const reshadefx::nodes::technique_declaration_node *node);
		void visit_pass(const reshadefx::nodes::pass_declaration_node *node, d3d10_pass &pass);
		void visit_pass_shader(const reshadefx::nodes::function_declaration_node *node, const std::string &shadertype, d3d10_pass &pass);

		d3d10_runtime *_runtime;
		bool _success;
		const reshadefx::syntax_tree &_ast;
		std::string &_errors;
		std::stringstream _global_code, _global_uniforms;
		bool _skip_shader_optimization;
		std::unordered_map<size_t, size_t> _sampler_descs;
		bool _is_in_parameter_block, _is_in_function_block;
	};
}