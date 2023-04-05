/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/

#include "symbol.hpp"
#include "muexcept.hpp"

#include <algorithm>

using namespace muesli;

Symbol::Symbol(const std::string& name) : Atom(),
					  m_value(nullptr),
					  m_function(nullptr),
					  is_constant(false)    
{
    if (name.empty())
    {
	throw InvalidSymbolNameExcept(name);
    }
    if (!std::isalpha(name[0]))
    {
	throw InvalidSymbolNameExcept(name, "must start with a letter");
    }
    for (char c : name)
    {
	if (!std::isalnum(c) && c != '-' && c != '_')
	{
	    throw InvalidSymbolNameExcept(name, "invalid characters");
	}
    }
    m_name.resize(name.size());
    std::transform(name.begin(), name.end(), m_name.begin(),
		   [](unsigned char c){ return std::toupper(c); });
}

const std::string&
Symbol::name() const
{
    return m_name;
}

SymbolPtr
Symbol::create(const std::string& s)
{
    return std::make_shared<Symbol>(s);
}
