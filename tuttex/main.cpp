// (C) Copyright David James Pearce and Gary Haggard, 2007.
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
// Email: david.pearce@mcs.vuw.ac.nz

#include <iostream>
#include <iomanip>
#include <fstream>
#include <stack>
#include <list>
#include <stdint.h>
#include <stdexcept>
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <csignal>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <ext/hash_map>

#include "../config.h"
#include "file_io.hpp"
#include "adjacency_list.hpp"
#include "directed_adjacency_list.hpp"
#include "nauty_graph.hpp"
#include "computation.hpp"
#include "factor_poly.hpp"

using namespace std;

// ---------------------------------------------------------------
// User-Defined Types
// ---------------------------------------------------------------

class my_timer
{
private:
  struct timeval _start;
  bool gtod;

public:
  my_timer(bool _gtod = true) : gtod(_gtod)
  {
    if (gtod)
    {
      gettimeofday(&_start, NULL);
    }
    else
    {
      struct rusage ru;
      getrusage(RUSAGE_SELF, &ru);
      _start = ru.ru_utime; // measure time spent in user space
    }
  }

  double elapsed(void)
  {
    struct timeval tmp;

    if (gtod)
    {
      gettimeofday(&tmp, NULL);
    }
    else
    {
      struct rusage ru;
      getrusage(RUSAGE_SELF, &ru);
      tmp = ru.ru_utime;
    }

    double end = tmp.tv_sec + (tmp.tv_usec / 1000000.0);
    double start = _start.tv_sec + (_start.tv_usec / 1000000.0);
    return end - start;
  }
};

typedef factor_poly<biguint> poly_t;
typedef adjacency_list<> graph_t;
typedef directed_adjacency_list<> dgraph_t;
typedef pair<unsigned int, unsigned int> edge_t;

// ---------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------

static unsigned long num_splits = 0;
static unsigned long num_leafs = 0;
static unsigned int num_isohits = 0;

// Following is used to time computation, and provide timeout
// facility.
static long timeout = 15768000; // one years worth of timeout (in s)
static my_timer global_timer(false);
static vector<pair<int, int>> evalpoints;
static unsigned int ngraphs_completed = 0;
static unsigned int cutoff_threshold = 0;
static bool quiet_flag = false;
static bool info_flag = false;
static bool status_flag = false;
static bool verbose_flag = false;
#define EDGESEL_AUTO 0
#define EDGESEL_DENSE 1
#define EDGESEL_SPARSE 2
static unsigned int edgesel_mode = EDGESEL_AUTO;
static bool dense_flag = false;
static bool chromatic_flag = false;

// ------------------------------------------------------------------
// Evalation Methods
// ------------------------------------------------------------------

poly_t chromatic(computation &comp, vector<unsigned int> const &order);
poly_t tutte(computation &comp, vector<unsigned int> const &order);

// ------------------------------------------------------------------
// Edge Selection
// ------------------------------------------------------------------

edge_t select_edge(unsigned char const *nauty_graph)
{
  unsigned int N = nauty_graph_numverts(nauty_graph);
  setword *cmap = nauty_graph_canong_map(nauty_graph);

  unsigned int mv = 0;
  unsigned int min = UINT_MAX;

  if (dense_flag)
  {
    for (unsigned int i = 0; i != N; ++i)
    {
      for (unsigned int j = (i + 1); j < N; ++j)
      {
        unsigned int ci = cmap[i];
        unsigned int cj = cmap[j];
        if (nauty_graph_is_edge(nauty_graph, ci, cj))
        {
          return edge_t(ci, cj);
        }
      }
    }
  }
  else
  {
    for (unsigned int i = 0; i != N; ++i)
    {
      unsigned int ci = cmap[i];
      unsigned int nv = nauty_graph_numedges(nauty_graph, ci);
      if (nv < min)
      {
        min = nv;
        mv = ci;
      }
    }

    for (unsigned int i = 0; i != N; ++i)
    {
      unsigned int ci = cmap[i];
      if (nauty_graph_is_edge(nauty_graph, mv, ci))
      {
        return edge_t(mv, ci);
      }
    }
  }

  return edge_t(UINT_MAX, UINT_MAX);
  //  throw std::runtime_error("internal failure (select_edge)");
}

edge_t select_edge(unsigned char const *nauty_graph, vector<edge_t> const &edgelist)
{
  unsigned int N = nauty_graph_numverts(nauty_graph);
  setword *cmap = nauty_graph_canong_map(nauty_graph);

  cout << "GOT HERE" << endl;

  for (vector<edge_t>::const_iterator i(edgelist.begin()); i != edgelist.end(); ++i)
  {
    cout << "STAGE 1" << endl;
    unsigned int ci = cmap[i->first];
    unsigned int cj = cmap[i->second];
    cout << "STAGE 2: " << i->first << "--" << i->second << " ===> " << ci << "--" << cj << endl
         << nauty_graph_str(nauty_graph) << endl;
    if (nauty_graph_is_edge(nauty_graph, ci, cj))
    {
      cout << "STAGE 3" << endl;
      return edge_t(ci, cj);
    }
    cout << "STAGE 4" << endl;
  }

  return edge_t(UINT_MAX, UINT_MAX);
  //  throw std::runtime_error("internal failure (select_edge)");
}

// ------------------------------------------------------------------
// Connectivity Test
// ------------------------------------------------------------------

#define CC_FOREST 1
#define CC_CONNECTED 2
#define CC_BICONNECTED 3

class cc_dat
{
public:
  unsigned int vindex;
  std::vector<bool> visited;
  std::vector<unsigned int> lowlink;
  std::vector<unsigned int> dfsnum;
  std::vector<unsigned int> cstack;

  void reset(unsigned int v)
  {
    vindex = 0;
    cstack.clear(); // make sure it's empty.
    visited.resize(v);
    lowlink.resize(v);
    dfsnum.resize(v);
    std::fill(visited.begin(), visited.end(), false);
  }
};

static cc_dat cc_data;
static vector<unsigned int> components;
static vector<unsigned int> component_ends;

// extract the biconncted component on the stack, and return the
// number of vertices involved.
void cc_extract(unsigned int v)
{
  unsigned int w = cc_data.cstack.back();

  while (cc_data.cstack.size() > 0 && w != v)
  {
    components.push_back(w);
    cc_data.cstack.pop_back();
    w = cc_data.cstack.back();
  }

  components.push_back(v);
  component_ends.push_back(components.size());
}

unsigned int cc_visit(unsigned int u, unsigned int v,
                      unsigned char const *graph)
{
  // traverse edge tail->head
  cc_data.dfsnum[v] = cc_data.vindex;
  cc_data.visited[v] = true;
  cc_data.lowlink[v] = cc_data.vindex++;
  cc_data.cstack.push_back(v);

  // now, consider edges
  unsigned int N = nauty_graph_numverts(graph);
  for (unsigned int i = 0; i != N; ++i)
  {
    if (nauty_graph_is_edge(graph, v, i))
    {
      if (!cc_data.visited[i])
      {

        cc_visit(v, i, graph);
        cc_data.lowlink[v] = std::min(cc_data.lowlink[v], cc_data.lowlink[i]);
        if (cc_data.lowlink[i] == cc_data.dfsnum[v])
        {
          // v is an articulation point separating
          // the component containing w from others.
          cc_extract(v);
        }
        else if (cc_data.lowlink[i] > cc_data.dfsnum[v])
        {
          // v is not in a biconnected component with w
          cc_data.cstack.pop_back();
        }
      }
      else if (i != u && cc_data.dfsnum[v] > cc_data.dfsnum[i])
      {
        // this is a back edge ...
        cc_data.lowlink[v] = std::min(cc_data.lowlink[v], cc_data.dfsnum[i]);
      }
    }
  }
}

// The following method traverses the input graph and attempts to
// determine whether or not it's biconnected.  If it's not
// biconnected, then it determines whether or not it's actually a
// tree.  If it's not a tree, then it identifies the first biconnected
// component and extracts its vertices to bicomp.
unsigned int check_connectivity(unsigned char const *graph)
{
  unsigned int N = nauty_graph_numverts(graph);
  unsigned int E = nauty_graph_numedges(graph);

  cc_data.reset(N);
  components.clear();
  component_ends.clear();

  for (unsigned int i = 0; i != N; ++i)
  {
    if (!cc_data.visited[i])
    {
      cc_visit(i, i, graph);
    }
  }

  if (component_ends.size() == 0)
  {
    return CC_FOREST;
  }
  else if (component_ends.size() == 1 && component_ends[0] == N)
  {
    return CC_BICONNECTED;
  }
  else
  {
    return CC_CONNECTED;
  }
}

// ------------------------------------------------------------------
// Build Computation Tree
// ------------------------------------------------------------------

void build(computation &comp, vector<edge_t> const &edgelist)
{
  while (comp.frontier_size() != 0)
  {
    if (verbose_flag)
    {
      cerr << "Generated " << comp.frontier_size() << " graphs, with " << num_splits << " splits, " << num_isohits << " hits and " << num_leafs << " leafs." << endl;
    }

    for (unsigned int i = 0; i != comp.frontier_size();)
    {
      unsigned int gindex = comp.frontier_get(i);
      unsigned char *nauty_graph = comp.graph_ptr(gindex);

      // cout << "G[" << gindex << "] = " << nauty_graph_str(nauty_graph) << endl;

      cout << "CHECKING CONNECTIVITY" << endl;
      unsigned int cinfo = check_connectivity(nauty_graph);
      cout << "DONE CHECKING CONNECTIVITY" << endl;

      switch (cinfo)
      {
      case CC_FOREST:
        // This indicates that the graph is actually a forest.
        // Therefore, we can terminate immediately.
        cout << "TERMINATE LEAF" << endl;
        comp.frontier_terminate(i);
        num_leafs++;
        break;
      case CC_CONNECTED:
        // This indicates that the original graph was not biconnected,
        // and that one or more biconnected components have been
        // extracted.  Therefore, we split on this biconnected
        // component.
        cout << "SPLITTING NODE" << endl;
        i += comp.frontier_split(i, components, component_ends);
        num_splits += TREE_NCHILDREN(comp.get(gindex));
        break;
      case CC_BICONNECTED:
      {
        // This indicates that the whole graph was biconnected.
        // Therefore, we have no choice but to perform a
        // delete-contract.
        cout << "SELECTING EDGE" << endl;
        unsigned int fsize = comp.frontier_size();
        edge_t edge = select_edge(nauty_graph, edgelist);

        cout << "DONE" << endl;

        if (edge.first != UINT_MAX)
        {
          cout << "DELETING " << edge.first << "--" << edge.second << endl;
          i += comp.frontier_delcontract(i, edge.first, edge.second);
          num_isohits += (fsize + 1) - comp.frontier_size();
          cout << "DONE" << endl;
        }
        else
        {
          comp.frontier_terminate(i);
        }

        break;
      }
      default:
        throw runtime_error("shouldn't get here");
      }
    }
  }

  if (verbose_flag)
  {
    cerr << "Generated computation tree with " << comp.size() << " nodes." << endl;
  }
}

// ------------------------------------------------------------------
// Order Computation Tree
// ------------------------------------------------------------------

void order_computation(computation &comp, std::vector<unsigned int> &order)
{
  // To actually compute the polynomial, I topologically sort the
  // computation DAG and the visit in reverse post-order.

  unsigned int N(comp.size());
  {
    // I use a seperate scope here to enable some memory reuse.
    dgraph_t dag(N);

    for (int i = 0; i != comp.size(); i++)
    {
      tree_node *tnode = comp.get(i);
      for (unsigned int j = 0; j != TREE_NCHILDREN(tnode); ++j)
      {
        unsigned int child = TREE_CHILD(tnode, j);
        dag.add_edge(i, child);
      }
    }

    if (verbose_flag)
    {
      cerr << "Built computation dag." << endl;
    }

    // Second, topologically sort the DAG
    topological_sort(dag, order);

    if (verbose_flag)
    {
      cerr << "Sorted computation dag." << endl;
    }
  }
}

// ---------------------------------------------------------------
// Enumerate Edge Choices
// ---------------------------------------------------------------
vector<vector<edge_t>> enumerate_edges_helper(vector<edge_t> edges, int count)
{
  vector<vector<edge_t>> enums;
  if (count == 1)
  {
    enums.push_back(edges);
  }
  else
  {
    for (unsigned int i = 0; i != edges.size(); ++i)
    {
      edge_t edge = edges[i];
      vector<edge_t> nedges = edges;
      nedges.erase(nedges.begin() + i);
      vector<vector<edge_t>> tmpenums = enumerate_edges_helper(nedges, count - 1);
      for (int j = 0; j != tmpenums.size(); ++j)
      {
        vector<edge_t> &en = tmpenums[j];
        en.push_back(edge);
        enums.push_back(en);
      }
    }
  }
  return enums;
}

vector<vector<edge_t>> enumerate_edges(graph_t const &graph, int count)
{
  vector<edge_t> edges;

  // now, consider edges
  for (graph_t::vertex_iterator i(graph.begin_verts()); i != graph.end_verts(); ++i)
  {
    unsigned int v = *i;
    for (graph_t::edge_iterator j(graph.begin_edges(v)); j != graph.end_edges(v); ++j)
    {
      unsigned int w = j->first;

      // now add this edge(s) to nauty graph
      if (v <= w)
      {
        edges.push_back(edge_t(v, w));
      }
    }
  }
  return enumerate_edges_helper(edges, count);
}

// ---------------------------------------------------------------
// Run Method
// ---------------------------------------------------------------

void reset_stats(unsigned int V)
{
  /*
  cache_hit_sizes.resize(V,0);
  cache.reset_stats();
  cache_hit_sizes.clear();
  num_steps = 0;
  old_num_steps = 0;
  num_bicomps = 0;
  num_disbicomps = 0;
  num_trees = 0;
  num_cycles = 0;
  */
}

void run(vector<graph_t> const &graphs, unsigned int beg, unsigned int end, uint64_t cache_size, unsigned int cache_buckets)
{
  computation comp(cache_size, cache_buckets);

  for (unsigned int i(beg); i < end; ++i)
  {
    unsigned int V = graphs[i].num_vertices();
    unsigned int E = graphs[i].num_edges();

    if (edgesel_mode == EDGESEL_AUTO)
    {
      double density = (2.0 * ((double)E)) / (((double)V) * ((double)V - 1));
      if (density < 0.5)
      {
        dense_flag = false;
      }
      else
      {
        dense_flag = true;
      }
    }
    else if (edgesel_mode == EDGESEL_SPARSE)
    {
      dense_flag = false;
    }
    else
    {
      dense_flag = true;
    }

    comp.clear();
    comp.initialise(graphs[i]);
    reset_stats(V);
    global_timer = my_timer(false);

    cout << "ENUMERATING EDGES" << endl;
    vector<vector<edge_t>> enums = enumerate_edges(graphs[i], 3);
    cout << "DONE" << endl;

    for (unsigned int j = 0; j != enums.size(); ++j)
    {
      vector<edge_t> const &en = enums[j];
      computation comp2(cache_size, cache_buckets);
      comp2.initialise(graphs[i]);
      build(comp2, enums[j]);
      cout << "FRONTIER SIZE: " << comp2.frontier_size() << endl;
    }

    if (!quiet_flag)
    {
      // first, order the computation prior to evaluation.
      vector<unsigned int> ordering;
      order_computation(comp, ordering);
      // second, evaluate the computation.
      poly_t poly;
      if (chromatic_flag)
      {
        poly = chromatic(comp, ordering);
      }
      else
      {
        poly = tutte(comp, ordering);
      }
      cout << poly.str() << endl;
    }

    if (info_flag)
    {
      cout << "=======" << endl;
      cout << "V = " << V << ", E = " << E << endl;
      cout << "Size of Computation Tree: " << comp.size() << " graphs." << endl;
      cout << "Number of isomorph hits: " << num_isohits << endl;
      cout << "Number of splits: " << num_splits << endl;
      cout << "Number of leafs: " << num_leafs << endl;
      cout << "Time : " << setprecision(3) << global_timer.elapsed() << "s" << endl;
    }
  }
}

// ---------------------------------------------------------------
// Misc Helpers Functions
// ---------------------------------------------------------------

typedef enum
{
  V_RANDOM,
  V_MINIMISE_UNDERLYING_DEGREE,
  V_MAXIMISE_UNDERLYING_DEGREE,
  V_MINIMISE_DEGREE,
  V_MAXIMISE_DEGREE,
  V_NONE
} vorder_t;

template <class G, class OP>
class vo_underlying
{
private:
  G const &graph;
  OP op;

public:
  vo_underlying(G const &g) : graph(g) {}

  bool operator()(unsigned int v1, unsigned int v2)
  {
    return op(graph.num_underlying_edges(v1), graph.num_underlying_edges(v2));
  }
};

template <class G, class OP>
class vo_multi
{
private:
  G const &graph;
  OP op;

public:
  vo_multi(G const &g) : graph(g) {}

  bool operator()(unsigned int v1, unsigned int v2)
  {
    return op(graph.num_edges(v1), graph.num_edges(v2));
  }
};

template <class G>
G permute_graph(G const &graph, vorder_t heuristic)
{
  vector<unsigned int> order;
  for (unsigned int i = 0; i != graph.num_vertices(); ++i)
  {
    order.push_back(i);
  }
  // obtain the new ordering
  switch (heuristic)
  {
  case V_RANDOM:
    random_shuffle(order.begin(), order.end());
    break;
  case V_MINIMISE_UNDERLYING_DEGREE:
    sort(order.begin(), order.end(), vo_underlying<G, less<unsigned int>>(graph));
    break;
  case V_MAXIMISE_UNDERLYING_DEGREE:
    sort(order.begin(), order.end(), vo_underlying<G, greater<unsigned int>>(graph));
    break;
  case V_MINIMISE_DEGREE:
    sort(order.begin(), order.end(), vo_multi<G, less<unsigned int>>(graph));
    break;
  case V_MAXIMISE_DEGREE:
    sort(order.begin(), order.end(), vo_multi<G, greater<unsigned int>>(graph));
    break;
  default:
    // do nothing
    break;
  }
  // transpose ordering
  vector<unsigned int> iorder(order.size());
  for (unsigned int i = 0; i != graph.num_vertices(); ++i)
  {
    iorder[order[i]] = i;
  }

  // finally, create new permuted graph
  G r(graph.num_vertices());

  for (typename G::vertex_iterator i(graph.begin_verts()); i != graph.end_verts(); ++i)
  {
    for (typename G::edge_iterator j(graph.begin_edges(*i));
         j != graph.end_edges(*i); ++j)
    {
      unsigned int head(*i);
      unsigned int tail(j->first);
      unsigned int count(j->second);
      if (head <= tail)
      {
        r.add_edge(iorder[head], iorder[tail], count);
      }
    }
  }
  return r;
}

pair<int, int> parse_evalpoint(char *str)
{
  char *endp = NULL;
  int a = strtol(str, &endp, 10);
  int b = strtol(endp + 1, &endp, 10);
  return make_pair(a, b);
}

uint64_t parse_amount(char *str)
{
  char *endp = NULL;
  long r = strtol(str, &endp, 10);
  if (*endp != '\0')
  {
    if (strcmp(endp, "M") == 0)
    {
      r = r * 1024 * 1024;
    }
    else if (strcmp(endp, "K") == 0)
    {
      r = r * 1024;
    }
    else if (strcmp(endp, "G") == 0)
    {
      r = r * 1024 * 1024 * 1024;
    }
  }
  return r;
}

// ---------------------------------------------------------------
// Main Method
// ---------------------------------------------------------------

int main(int argc, char *argv[])
{

  // ------------------------------
  // Process command-line arguments
  // ------------------------------

#define OPT_HELP 0
#define OPT_QUIET 1
#define OPT_VERBOSE 2
#define OPT_INFO 3
#define OPT_VERSION 4
#define OPT_SMALLGRAPHS 5
#define OPT_GRAPHS 6
#define OPT_TIMEOUT 7
#define OPT_EVALPOINT 8
#define OPT_CUTOFF 9
#define OPT_CACHESIZE 10
#define OPT_CACHEBUCKETS 11
#define OPT_CHROMATIC 12
#define OPT_AUTOSEL 20
#define OPT_DENSE 21
#define OPT_SPARSE 22

  struct option long_options[] = {
      {"help", no_argument, NULL, OPT_HELP},
      {"version", no_argument, NULL, OPT_VERSION},
      {"info", no_argument, NULL, OPT_INFO},
      {"quiet", no_argument, NULL, OPT_QUIET},
      {"verbose", no_argument, NULL, OPT_VERBOSE},
      {"cutoff", required_argument, NULL, OPT_CUTOFF},
      {"chromatic", no_argument, NULL, OPT_CHROMATIC},
      {"cache-size", required_argument, NULL, OPT_CACHESIZE},
      {"cache-buckets", required_argument, NULL, OPT_CACHEBUCKETS},
      {"dense", no_argument, NULL, OPT_DENSE},
      {"sparse", no_argument, NULL, OPT_SPARSE},
      NULL};

  char const *descriptions[] = {
      "        --help                    display this information",
      "        --version                 display the version number of this program",
      " -i     --info                    output summary information regarding computation",
      " -q     --quiet                   output info summary as single line only (useful for generating data)",
      " -v     --verbose                 display lots of information useful for debugging.",
      " -s<x>  --cutoff=<x>              stop the computation at graphs with fewer than x vertices",
      " -g<x:y>  --graphs=<start:end>    which graphs to process from input file, e.g. 2:10 processes the 2nd to tenth inclusive",
      " \nevaluation options:",
      "        --chromatic               compute the chromatic polynomial",
      " \ncache options:",
      " -c<x>  --cache-size=<amount>     set sizeof cache to allocate, e.g. 700M",
      "        --cache-buckets=<amount>  set number of buckets to use in cache, e.g. 10000",
  };

  vorder_t vertex_ordering(V_MAXIMISE_UNDERLYING_DEGREE);
  uint64_t cache_size(256 * 1024 * 1024);
  unsigned int cache_buckets = 100000;
  unsigned int beg = 0;
  unsigned int end = UINT_MAX - 1;
  unsigned int v;

  while ((v = getopt_long(argc, argv, "qic:g:s:", long_options, NULL)) != -1)
  {
    switch (v)
    {
    case OPT_HELP:
      cout << "usage: " << argv[0] << " [options] <input graph file>" << endl;
      cout << "options:" << endl;
      for (char const **ptr = descriptions; *ptr != NULL; ptr++)
      {
        cout << *ptr << endl;
      }
      exit(1);
    case OPT_VERSION:
      cout << "Tutte version " << VERSION << endl;
      cout << "Developed by David J. Pearce, Gary Haggard and Gordon Royle, 2008" << endl;
      exit(0);
      break;
    case 'q':
    case OPT_QUIET:
      quiet_flag = true;
      break;
    case OPT_VERBOSE:
      verbose_flag = true;
      break;
    case 'i':
    case OPT_INFO:
      info_flag = true;
      break;
    case 't':
    case OPT_TIMEOUT:
      timeout = atoi(optarg);
      break;
    case 's':
    case OPT_CUTOFF:
      cutoff_threshold = atoi(optarg);
      break;
    case OPT_CHROMATIC:
      chromatic_flag = true;
      break;
    case 'g':
    case OPT_GRAPHS:
    {
      string s(optarg);
      unsigned int pos = 0;
      beg = parse_number(pos, s);
      match(':', pos, s);
      end = parse_number(pos, s);
      break;
    }
    case OPT_DENSE:
      edgesel_mode = EDGESEL_DENSE;
      break;
    case OPT_SPARSE:
      edgesel_mode = EDGESEL_SPARSE;
      break;
    // --- CACHE OPTIONS ---
    case 'c':
    case OPT_CACHESIZE:
      cache_size = parse_amount(optarg);
      break;
    case OPT_CACHEBUCKETS:
      cache_buckets = parse_amount(optarg);
      break;
    default:
      cout << "Unrecognised parameter!" << endl;
      exit(1);
    }
  }

  // Quick sanity check

  if (optind >= argc)
  {
    cout << "usage: " << argv[0] << " [options] <input graph file>" << endl;
    cout << "options:" << endl;
    for (char const **ptr = descriptions; *ptr != NULL; ptr++)
    {
      cout << *ptr << endl;
    }
    exit(1);
  }

  // -------------------------------------------------
  // Initialise Cache
  // -------------------------------------------------

  try
  {
    // -----------------------------------
    // Now, begin solving the input graph!
    // -----------------------------------

    ifstream inputfile(argv[optind]);
    vector<graph_t> graphs = read_file<graph_t>(inputfile);

    for (unsigned int i = 0; i != graphs.size(); ++i)
    {
      graphs[i] = permute_graph(graphs[i], vertex_ordering);
    }

    run(graphs, beg, std::min<unsigned int>(graphs.size(), end + 1), cache_size, cache_buckets);
  }
  catch (std::runtime_error &e)
  {
    cerr << "error: " << e.what() << endl;
  }
  catch (std::bad_alloc &e)
  {
    cerr << "error: insufficient memory!" << endl;
  }
  catch (std::exception &e)
  {
    cerr << "error: " << e.what() << endl;
  }
}
