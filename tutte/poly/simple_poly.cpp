// (C) Copyright David James Pearce and Gary Haggard, 2007.
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
// Email: david.pearce@mcs.vuw.ac.nz

#include "simple_poly.hpp"

using namespace std;

const simple_poly operator+(simple_poly const &p1, simple_poly const &p2)
{
  simple_poly r(p1);

  for (map<term, unsigned int>::const_iterator i(p2.terms.begin()); i != p2.terms.end(); ++i)
  {
    map<term, unsigned int>::iterator j = r.terms.find(i->first);
    if (j != r.terms.end())
    {
      j->second += i->second;
    }
    else
    {
      r.terms.insert(std::make_pair(i->first, i->second));
    }
  }

  return r;
}

// this is gary's shift operation
const simple_poly operator*(simple_poly const &p1, term const &p2)
{
  simple_poly r;

  for (map<term, unsigned int>::const_iterator i(p1.terms.begin()); i != p1.terms.end(); ++i)
  {
    term t(i->first);
    t.xpower += p2.xpower;
    t.ypower += p2.ypower;

    r.terms.insert(std::make_pair(t, i->second));
  }

  return r;
}
