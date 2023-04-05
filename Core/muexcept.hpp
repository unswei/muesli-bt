/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_MUEXCEPT_HPP
#define MUESLISP_MUEXCEPT_HPP

#include "token.hpp"

#include <stdexcept>

namespace muesli
{

class InvalidSymbolNameExcept : public std::invalid_argument
{
public:
    explicit InvalidSymbolNameExcept()
	: std::invalid_argument("Invalid symbol name: (empty string)")
    {}

    explicit InvalidSymbolNameExcept(const std::string& symbol)
	: std::invalid_argument("Invalid symbol name: `" + symbol + "`")
    {}

    explicit InvalidSymbolNameExcept(const std::string& symbol,
	                             const std::string& detail)
	: std::invalid_argument("Invalid symbol name: `" + symbol +
				"` (" + detail + ")")
    {}
};

class InvalidTokenExcept : public std::invalid_argument
{
public:
    explicit InvalidTokenExcept()
	: std::invalid_argument("Invalid token (T_INVALID)")
    {}

    explicit InvalidTokenExcept(const std::string& token)
	: std::invalid_argument("Invalid token: `'" + token + "`")
    {}

    explicit InvalidTokenExcept(Token::Type t,
				const std::string& token)
	: std::invalid_argument("Invalid token (" + toString(t) +
				"): `'" + token + "`")
    {}
};

class SyntaxError : public std::runtime_error    
{
public:
    explicit SyntaxError()
	: std::runtime_error("Syntax error (no input)")
    {}

    explicit SyntaxError(const std::string& issue)
	: std::runtime_error("Syntax error (" + issue +")")
    {}
};
}
#endif // MUESLISP_MUEXCEPT_HPP
