/** Copyright (c) 2022 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLISP_SYMBOL_HPP
#define MUESLISP_SYMBOL_HPP

#include "atom.hpp"

namespace muesli
{
class Symbol;    
using SymbolPtr = std::shared_ptr<Symbol>;
    
class Symbol : public Atom
{
public:
    explicit Symbol(const std::string& name);
    
    const std::string& name() const;

    std::string toString() const override
    { return m_name; }

    static SymbolPtr create(const std::string& s);

    bool operator <(const Symbol& s) const;

private:
    std::string m_name;
    CellPtr m_value;
    CellPtr m_function;
    bool is_constant;
};

    
}

inline bool
muesli::Symbol::operator <(const muesli::Symbol& s) const
{
    return name() < s.name();
}

#endif // MUESLISP_SYMBOL_HPP
