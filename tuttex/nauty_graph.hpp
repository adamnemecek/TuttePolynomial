// (C) Copyright David James Pearce and Gary Haggard, 2007.
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
// Email: david.pearce@mcs.vuw.ac.nz

#ifndef NAUTY_GRAPH_HPP
#define NAUTY_GRAPH_HPP

#include <iostream>
#include <string>
#include <vector>

// the following is needed for Nauty and determines
// the maximum graph size
#define MAXN 0
#define NAUTY_HEADER_SIZE 3
#include "nauty.h"

// ----------------------------------
// METHODS FOR INTERFACING WITH NAUTY
// ----------------------------------

// test if two nauty graphs are equal.
bool nauty_graph_equals(unsigned char const *g1, unsigned char const *g2);

// determine hash code for nauty graph.
unsigned int nauty_graph_hashcode(unsigned char const *graph);

size_t nauty_graph_size(unsigned char const *key);

// determine size of nauty graph.
inline size_t nauty_graph_size(unsigned int NN)
{
  setword M = ((NN % WORDSIZE) > 0) ? (NN / WORDSIZE) + 1 : NN / WORDSIZE;
  return (NN * M) + NN + NAUTY_HEADER_SIZE;
}

inline size_t nauty_graph_numverts(unsigned char const *graph)
{
  setword *p = (setword *)graph;
  return p[1];
}

inline size_t nauty_graph_numedges(unsigned char const *graph)
{
  setword *p = (setword *)graph;
  return p[2];
}

inline size_t nauty_graph_numedges(unsigned char const *graph, unsigned int v)
{
  setword *p = (setword *)graph;
  setword NN = p[1];
  setword M = ((NN % WORDSIZE) > 0) ? (NN / WORDSIZE) + 1 : NN / WORDSIZE;
  setword *buffer = p + NAUTY_HEADER_SIZE + (M * v);

  unsigned int count = 0;
  for (unsigned int i = 0; i != NN; ++i)
  {
    unsigned int wb = (i / WORDSIZE);
    unsigned int wo = i - (wb * WORDSIZE);
    setword mask = (((setword)1U) << (WORDSIZE - wo - 1));
    if (buffer[wb] & mask)
    {
      count++;
    }
  }
  return count;
}

inline bool nauty_graph_is_edge(unsigned char const *graph, unsigned int from, unsigned int to)
{
  setword *p = (setword *)graph;
  setword NN = p[1];
  setword M = ((NN % WORDSIZE) > 0) ? (NN / WORDSIZE) + 1 : NN / WORDSIZE;
  setword *buffer = p + NAUTY_HEADER_SIZE;

  unsigned int wb = (from / WORDSIZE);
  unsigned int wo = from - (wb * WORDSIZE);
  setword mask = (((setword)1U) << (WORDSIZE - wo - 1));
  if (buffer[(to * M) + wb] & mask)
  {
    return true;
  }
  return false;
}

// add an edge to a nauty graph.
bool nauty_graph_add(unsigned char *graph, unsigned int from, unsigned int to);

// delete an edge from a nauty graph.
bool nauty_graph_delete(unsigned char *graph, unsigned int from, unsigned int to);

// delete a vertex from a nauty graph.
void nauty_graph_delvert(unsigned char const *input, unsigned char *output, unsigned int vertex);

// Extract a subgraph from the input graph.  The subgraph is
// determined by the vertices in the component list.
void nauty_graph_extract(unsigned char *graph, unsigned char *output, unsigned int const *component, unsigned int N);

// make an exact copy of a nauty graph.
void nauty_graph_clone(unsigned char const *graph, unsigned char *output);

// create a canonical labelling of the nauty graph, writing it into
// output.
void nauty_graph_canon(unsigned char const *key, unsigned char *output);

inline setword *nauty_graph_canong_map(unsigned char const *graph)
{
  setword *p = (setword *)graph;
  setword NN = p[1];
  setword M = ((NN % WORDSIZE) > 0) ? (NN / WORDSIZE) + 1 : NN / WORDSIZE;

  return p + NAUTY_HEADER_SIZE + (NN * M);
}

// the following method can be implemented without copying.
void nauty_graph_canong_delete(unsigned char const *graph, unsigned char *output, unsigned int from, unsigned int to);

// not sure about the following method.
void nauty_graph_canong_contract(unsigned char const *graph, unsigned char *output, unsigned int from, unsigned int to, bool loops = true);

std::string nauty_graph_str(unsigned char const *graph);

// Construct a nauty graph from a general graph, such as adjlist.
template <class T>
unsigned char *nauty_graph_build(T const &graph)
{
  setword N = graph.num_vertices();
  setword NN = N + graph.num_multiedges();
  setword M = ((NN % WORDSIZE) > 0) ? (NN / WORDSIZE) + 1 : NN / WORDSIZE;

  setword *nauty_graph_buf = new setword[(NN * M) + NAUTY_HEADER_SIZE + NN];

  memset(nauty_graph_buf, 0, ((NN * M) + NAUTY_HEADER_SIZE + NN) * sizeof(setword));

  nauty_graph_buf[0] = N;
  nauty_graph_buf[1] = NN;
  nauty_graph_buf[2] = 0;

  // build map from graph vertex space to nauty vertex space
  unsigned int vtxmap[graph.domain_size()];
  unsigned int idx = 0;
  for (typename T::vertex_iterator i(graph.begin_verts());
       i != graph.end_verts(); ++i, ++idx)
  {
    vtxmap[*i] = idx;
  }

  // now, build nauty graph.
  int mes = N; // multi-edge start
  for (typename T::vertex_iterator i(graph.begin_verts()); i != graph.end_verts(); ++i)
  {
    unsigned int _v = *i;
    for (typename T::edge_iterator j(graph.begin_edges(_v)); j != graph.end_edges(_v); ++j)
    {
      unsigned int _w = j->first;

      // convert vertices into nauty graph vertex space
      unsigned int v = vtxmap[_v];
      unsigned int w = vtxmap[_w];

      // now add this edge(s) to nauty graph
      if (v <= w)
      {
        nauty_graph_add((unsigned char *)nauty_graph_buf, v, w);
        unsigned int k = j->second - 1;
        if (k > 0)
        {
          // this is a multi-edge!
          for (; k != 0; --k, ++mes)
          {
            nauty_graph_add((unsigned char *)nauty_graph_buf, v, mes);
            nauty_graph_add((unsigned char *)nauty_graph_buf, mes, w);
          }
        }
      }
    }
  }

  setword *mapping = nauty_graph_canong_map((unsigned char *)nauty_graph_buf);
  for (unsigned int i = 0; i != NN; ++i)
  {
    mapping[i] = i;
  }

  return (unsigned char *)nauty_graph_buf;
}

template <class T>
T from_nauty_graph(unsigned char *key)
{
  setword *p = (setword *)key;
  setword N = p[0];
  setword REAL_N = p[1];
  setword M = ((N % WORDSIZE) > 0) ? (N / WORDSIZE) + 1 : N / WORDSIZE;
  p = p + NAUTY_HEADER_SIZE;

  T graph(REAL_N); // should make real N

  // first, deal with normal edges
  for (int i = 0; i != REAL_N; ++i)
  {
    for (int j = i; j != REAL_N; ++j)
    {
      unsigned int wb = (i / WORDSIZE);
      unsigned int wo = i - (wb * WORDSIZE);

      setword mask = (1U << (WORDSIZE - wo - 1));
      if (p[(j * M) + wb] & mask)
      {
        graph.add_edge(i, j);
      }
    }
  }

  // second, deal with multi-edges
  for (int i = REAL_N; i != N; ++i)
  {
    unsigned int y = N;
    unsigned int x = N;
    for (int j = 0; j != REAL_N; ++j)
    {
      unsigned int wb = (i / WORDSIZE);
      unsigned int wo = i - (wb * WORDSIZE);

      setword mask = (1U << (WORDSIZE - wo - 1));
      if (p[(j * M) + wb] & mask)
      {
        y = x;
        x = j;
      }
    }
    graph.add_edge(x, y);
  }

  return graph;
}

// ------------------------------------
// A C++ Wrapper class for nauty graphs
// ------------------------------------

class nauty_graph
{
private:
  unsigned char *buffer;

public:
  inline nauty_graph(unsigned int N = 0)
  {
    setword M = ((N % WORDSIZE) > 0) ? (N / WORDSIZE) + 1 : N / WORDSIZE;
    buffer = new unsigned char[N * M];
  }

  inline nauty_graph(nauty_graph const &graph)
  {
    buffer = new unsigned char[nauty_graph_size(graph.buffer)];
    nauty_graph_clone(graph.buffer, buffer);
  }

  inline ~nauty_graph()
  {
    delete[] buffer;
  }

  inline unsigned int num_vertices() const
  {
    return buffer[0];
  }

  inline unsigned char const *getbuffer() const
  {
    return buffer;
  }

  inline unsigned int buffer_size() const
  {
    return nauty_graph_size(buffer);
  }

  inline bool add_edge(unsigned int from, unsigned int to)
  {
    nauty_graph_add(buffer, from, to);
  }

  inline void delete_edge(unsigned int from, unsigned int to)
  {
    nauty_graph_delete(buffer, from, to);
  }

  inline nauty_graph &operator=(nauty_graph const &ng);

  inline bool operator==(nauty_graph const &ng) const
  {
    return nauty_graph_equals(buffer, ng.buffer);
  }
};

#endif
