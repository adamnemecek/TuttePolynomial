#include <iostream>
#include <fstream>
#include <stack>
#include <stdexcept>
#include <algorithm>

#include "config.h"
#include "graph/algorithms.hpp"

using namespace std;

// ---------------------------------------------------------------
// Global Variables
// ---------------------------------------------------------------

stack<Graph> worklist; // the worklist!

// ---------------------------------------------------------------
// Method Bodies
// ---------------------------------------------------------------

/* deleteContract is the core algorithm for the tutte computation
 * it reduces a graph to two smaller graphs using a delete operation
 * for one, and a contract operation for the other.
 *
 * The algorithm also uses a number of tricks to prune the computation
 * space.  These include: eliminating small graphs using optimised, 
 * hand-coded decision procedures; storing previously seen graphs
 * in a cache; and, monitoring the "treeness" of the graph.
 */

void deleteContract(Poly &tutte) { 
  int start_size = worklist.size(); // size of stack on entry
  
  // keep going until all branches from entry point are explored
  
  while(worklist.size() > start_size) { 
    Graph &g = worklist.top();  // take reference to avoid copying!

    // if the graph is a tree, then we're done.
    if(g.is_tree()) {
      // This isn't quite right.  Need to multiply by the number of edges?
      tutte.mulByX();      
    } else if (g.is_loop()) {
      tutte.mulByY();
    } else {

      // at this point, there are several things we can do:
      // 
      // 1) eliminate small graphs using Gary's hard-coded
      //    algorithms.
      //
      // 2) lookup this graph in the cache to see if we've
      //    solved it before.
      //
      // At this stage, I'm not going to do either for 
      // simplicity ...
      
      // now, select the edge to remove
      int f, t;
      // this is a real simple algorithm for now!
      for(Graph::vertex_iterator i(g.begin_verts());i!=g.end_verts();++i) {
	for(Graph::edge_iterator j(g.begin_edges(*i));j!=g.end_edges(*j);++j) {
	  f = *i;
	  t = *j;
	  goto loop_exit; // YUK
	}
      }
      
    loop_exit:
      // copy the graph
      worklist.push(g);
      Graph &g2 = worklist.top();
      g.contract_edge(f,t);
      g2.remove_edge(f,t);  
      
      deleteContract(tutte);
    }    
  }  
}

// the following is a really simple file parser

int parse_number(std::istream &input) {
  int r;
  input >> r;
  return r;
}

void parse_comma(std::istream &input) {
  char c;
  input >> c;
}

Graph read_graph(std::istream &input) {
  vector<pair<unsigned int, unsigned int> > edgelist;
  unsigned int V=0;
    
  bool firstTime=true;

  while(!input.eof()) {
    if(!firstTime) { parse_comma(input); }
    if(input.eof()) { break; }
    firstTime=false;
    // just keep on reading!
    unsigned int tail = parse_number(input);
    parse_comma(input);
    unsigned int head = parse_number(input);
    V = max(V,max(head,tail));
    edgelist.push_back(std::make_pair(tail,head));
  }  

  Graph r(V+1);

  for(vector<pair<unsigned int, unsigned int> >::iterator i(edgelist.begin());
      i!=edgelist.end();++i) {
    r.add_edge(i->first,i->second);
  }

  return r;
}

// ---------------------------------------------------------------
// Main Method
// ---------------------------------------------------------------

int main(int argc, char *argv[]) {

  Poly tuttePolynomial;   // the solution will be built into this
  Graph start_graph(0);
  try {
    ifstream input(argv[1]);
    start_graph = read_graph(input);
  
    cout << "VERTICES = " << start_graph.num_vertices() << ", EDGES = " << start_graph.num_edges() << endl << endl;
    
    worklist.push(start_graph);
    deleteContract(tuttePolynomial);
        
    print_graph(cout,start_graph);

    // printPoly(tuttePolynomial);
  } catch(exception const &e) {
    cout << "error: " << e.what() << endl;
  }
}






                                            
