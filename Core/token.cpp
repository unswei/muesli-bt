/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#include "token.hpp"

#include "muexcept.hpp"
#include "string.hpp"
#include "symbol.hpp"

#include <algorithm>

using namespace muesli;

Token::Token(Token::Type t) : m_type(t), m_valuep(nullptr)
{
    switch (m_type)
    {
    case Type::T_LP:
	m_c = '(';
	break;
    case Type::T_RP:
	m_c = ')';
	break;
    case Type::T_INVALID:
	throw InvalidTokenExcept();	
    default:
	throw InvalidTokenExcept(t, "no value provided");
    }
}

Token::Token(Token::Type t, const std::string& s) :
    m_type(t), m_c(' '), m_valuep(nullptr)
{
    switch (m_type)
    {
    case Type::T_STRING:
	m_valuep = String::create(s);
	break;
    case Type::T_SYMBOL:
	m_valuep = Symbol::create(s);
	break;
    case Type::T_NUMBER: // may throw invalid_argument or out_of_range
    {
	size_t pos = 0;
	int i = std::stoi(s);
	if (pos == s.size())
	{
	    m_valuep = Atom::create(i);
	}
	else
	{
	    double d = std::stod(s);
	    m_valuep = Atom::create(d);
	}
	break;
    }
    case Type::T_LP:
	m_c = '(';
	break;
    case Type::T_RP:
	m_c = ')';
	break;
    default:
	throw InvalidTokenExcept(s);
    }
}


void
Token::ltrim(std::string& s)
{
    s.erase(s.begin(),
	    std::find_if(s.begin(), s.end(),
			 [](unsigned char c) { return !std::isspace(c); })
	);
}

void
Token::rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(), s.rend(),
			 [](unsigned char c) { return !std::isspace(c); }
		).base(),
	    s.end());
}


std::string
muesli::toString(Token::Type t)
{
    switch(t)
    {
    case Token::Type::T_STRING:
	return "T_STRING";
    case Token::Type::T_SYMBOL:
	return "T_SYMBOL";
    case Token::Type::T_NUMBER:
	return "T_NUMBER";
    case Token::Type::T_LP:
	return "T_LP";
    case Token::Type::T_RP:
	return "T_RP";
    case Token::Type::T_INVALID:
	return "T_INVALID";
    default:
	return "UNKNOWN";
    }
}
