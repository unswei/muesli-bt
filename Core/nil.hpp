/** Copyright (c) 2023 Oliver Obst                            -*- mode: c++ -*-
    This file is part of mueslisp
**/
#ifndef MUESLIP_NIL_HPP
#define MUESLIP_NIL_HPP

#include "atom.hpp"

namespace muesli
{
class NIL;
class TRU;
    
using NILPtr = std::shared_ptr<NIL>;    
using TRUPtr = std::shared_ptr<TRU>;

class NIL : public Atom
{
public:
    static const NILPtr& instance()
    {
	static const NILPtr nil_instance = NILPtr(new NIL());
	return nil_instance;
    }
    
    bool isNIL() const override
    { return true; }

    std::string toString() const override
    { return "NIL"; }

private:
    NIL() {}
};

class TRU : public Atom
{
public:
    static const TRUPtr& instance()
    {
	static const TRUPtr tru_instance = TRUPtr(new TRU());
	return tru_instance;
    }
    
    std::string toString() const override
    { return "t"; }

private:
    TRU() {}
};
    
extern const NILPtr nil;    
extern const TRUPtr tru;    
}

#endif // MUESLISP_NIL_HPP

