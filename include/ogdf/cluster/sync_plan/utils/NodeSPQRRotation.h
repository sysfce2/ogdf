/** \file
 * \brief TODO Document
 *
 * \author Simon D. Fink <ogdf@niko.fink.bayern>
 *
 * \par License:
 * This file is part of the Open Graph Drawing Framework (OGDF).
 *
 * \par
 * Copyright (C)<br>
 * See README.md in the OGDF root directory for details.
 *
 * \par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 or 3 as published by the Free Software Foundation;
 * see the file LICENSE.txt included in the packaging of this file
 * for details.
 *
 * \par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * \par
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see
 * http://www.gnu.org/copyleft/gpl.html
 */
#pragma once

#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphCopy.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/pctree/NodePCRotation.h>
#include <ogdf/basic/pctree/PCNode.h>
#include <ogdf/basic/pctree/PCRegistry.h>
#include <ogdf/basic/pctree/PCTreeIterators.h>
#include <ogdf/basic/pctree/util/IntrusiveList.h>
#include <ogdf/decomposition/BCTree.h>
#include <ogdf/decomposition/DynamicSPQRForest.h>

#include <functional>

namespace ogdf {
class Logger;
template<class E>
class SList;
} // namespace ogdf

#pragma GCC diagnostic ignored "-Wshadow" // TODO remove

namespace ogdf::sync_plan {

class NodeSPQRRotation : public pc_tree::NodePCRotation {
protected:
	const DynamicSPQRForest& spqr;
	node apex;

	const NodeArray<GraphCopySimple*>& rigids;
	NodeArray<node> highest_with_edges;
	NodeArray<SList<adjEntry>> edges;
	NodeArray<SList<node>> children;

	node findSPQRApex(node n, bool clear = false);

	pc_tree::PCNode* addLeaf(pc_tree::PCNode* n, adjEntry adj);

	pc_tree::PCNode* makePCNode(node t, node t_parent, pc_tree::PCNode* parent);

	struct RigidEmbedding {
		// room for improvement: mostly needed for the destructor clean-up, because NodeArray<unique_ptr> didn't compile previously
		const DynamicSPQRForest spqr;
		NodeArray<GraphCopySimple*> rigids;

		RigidEmbedding(Graph& G);

		~RigidEmbedding() {
			for (node n : spqr.spqrTree().nodes) {
				delete rigids[n];
			}
		}
	};

public:
	static Logger logger;

	explicit NodeSPQRRotation(const DynamicSPQRForest& spqr, node n,
			const NodeArray<GraphCopySimple*>& rigids)
		: spqr(spqr)
		, rigids(rigids)
		, highest_with_edges(spqr.spqrTree(), nullptr)
		, edges(spqr.spqrTree())
		, children(spqr.spqrTree()) {
		OGDF_ASSERT(n != nullptr);
		OGDF_ASSERT(n->graphOf() == &spqr.auxiliaryGraph());
		OGDF_ASSERT(spqr.spqrroot(spqr.bccomp(n)) != nullptr);
		OGDF_ASSERT(spqr.spqrproper(n->firstAdj()->theEdge()) != nullptr);
		m_G = &spqr.auxiliaryGraph();
		m_n = n;
		m_incidentEdgeForLeaf.init(*this, nullptr);
		m_graphNodeForInnerNode.init(*this, nullptr);
		apex = findSPQRApex(n);
		pc_tree::PCNode* root = makePCNode(apex, nullptr, nullptr);
		// findSPQRApex(n, true); // clear arrays
		OGDF_ASSERT(getRootNode() == root);
		node o = spqr.original(n);
		if (spqr.typeOfGNode(o) == BCTree::GNodeType::Normal) {
			OGDF_ASSERT(getLeafCount() == o->degree());
		} else {
			OGDF_ASSERT(getLeafCount() <= o->degree());
		}
		OGDF_ASSERT(checkValid());
		// room for improvement: reuse highest, edges and children arrays
	}

	void mapPartnerEdges();

	void mapGraph(const Graph* G, const std::function<node(node)>& nodes,
			const std::function<edge(edge)>& edges) {
		OGDF_ASSERT(G != nullptr);
		m_G = G;
		m_n = nodes(m_n);
		for (pc_tree::PCNode* n : pc_tree::FilteringPCTreeDFS(*this, getRootNode())) {
			node& gn = m_graphNodeForInnerNode[n];
			if (gn != nullptr) {
				gn = nodes(gn);
			}
		}
		for (pc_tree::PCNode* l : getLeaves()) {
			edge& ie = m_incidentEdgeForLeaf[l];
			if (ie != nullptr) {
				ie = edges(ie);
			}
			if (knowsPartnerEdges()) {
				for (edge& e : m_bundleEdgesForLeaf[l]) {
					e = edges(e);
				}
			}
		}
	}
};

}
