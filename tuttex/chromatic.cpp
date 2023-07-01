#include "biguint.hpp"
#include "computation.hpp"
#include "factor_poly.hpp"

using namespace std;

typedef factor_poly<biguint> poly_t;

// ------------------------------------------------------------------
// Chromatic Polynomial Evaluation
// ------------------------------------------------------------------

poly_t chromatic(computation &comp, vector<unsigned int> const &order)
{
	unsigned int N(comp.size());
	vector<poly_t> polys(N);

	for (int i = 0; i != N; ++i)
	{
		unsigned int n = order[i];
		tree_node *tnode = comp.get(order[i]);

		switch (TREE_TYPE(tnode))
		{
		case TREE_CONSTANT:
		{
			unsigned int nedges = nauty_graph_numedges(comp.graph_ptr(n));
			// cout << "P[" << n << "] = " << "x^" << nedges << endl;
			polys[n] = X(nedges);
			//	cout << "G[" << n << "] = " << nauty_graph_str(comp.graph_ptr(n)) << endl;
			break;
		}
		case TREE_SUM:
		{
			unsigned int lhs = TREE_CHILD(tnode, 0);
			unsigned int rhs = TREE_CHILD(tnode, 1);
			polys[n] = polys[lhs] + polys[rhs];
			//	cout << "P[" << n << "] = " << "P[" << lhs << "] + P[" << rhs << "] = " << polys[n].str() << endl;
			//	cout << "G[" << n << "] = " << nauty_graph_str(comp.graph_ptr(n)) << endl;
			break;
		}
		case TREE_PRODUCT:
		{
			//	cout << "P[" << n << "] = ";
			for (unsigned int j = 0; j != TREE_NCHILDREN(tnode); ++j)
			{
				unsigned int child = TREE_CHILD(tnode, j);
				if (j == 0)
				{
					polys[n] = polys[child];
				}
				else
				{
					//	    cout << "* ";
					polys[n] *= polys[child];
				}
				//	  cout << "P[" << child << "] ";
			}
			//	cout << "= " << polys[n].str()  << endl;
			//	cout << "G[" << n << "] = " << nauty_graph_str(comp.graph_ptr(n)) << endl;
			break;
		}
		}
	}

	return polys[0];
}
