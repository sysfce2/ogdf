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
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/Logger.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/simple_graph_alg.h>
#include <ogdf/cluster/ClusterGraph.h>
#include <ogdf/cluster/ClusterGraphAttributes.h>
#include <ogdf/cluster/sync_plan/ClusterPlanarity.h>
#include <ogdf/cluster/sync_plan/PMatching.h>
#include <ogdf/cluster/sync_plan/SyncPlan.h>
#include <ogdf/cluster/sync_plan/basic/GraphUtils.h>
#include <ogdf/cluster/sync_plan/utils/Bijection.h>
#include <ogdf/cluster/sync_plan/utils/Logging.h>

#include <sstream>
#include <string>

using namespace ogdf::sync_plan::internal;

bool ogdf::SyncPlanClusterPlanarityModule::isClusterPlanarDestructive(ClusterGraph& CG, Graph& G) {
	sync_plan::SyncPlan SP(&G, &CG);
	return SP.makeReduced() && SP.solveReduced();
}

bool ogdf::SyncPlanClusterPlanarityModule::clusterPlanarEmbedClusterPlanarGraph(ClusterGraph& CG,
		Graph& G) {
	sync_plan::SyncPlan SP(&G, &CG, m_augmentation);
	if (SP.makeReduced() && SP.solveReduced()) {
		SP.embed();
		return true;
	} else {
		return false;
	}
}

bool ogdf::SyncPlanClusterPlanarityModule::clusterPlanarEmbed(ogdf::ClusterGraph& CG, ogdf::Graph& G) {
	OGDF_ASSERT(&CG.constGraph() == &G);
	Graph Gcopy;
	ClusterArray<cluster> copyC(CG, nullptr);
	NodeArray<node> copyN(G, nullptr);
	EdgeArray<edge> copyE(G, nullptr);
	ClusterGraph CGcopy(CG, Gcopy, copyC, copyN, copyE);

	EdgeArray<edge> origE(Gcopy, nullptr);
	invertRegisteredArray(copyE, origE);

	sync_plan::SyncPlan SP(&Gcopy, &CGcopy, m_augmentation);
	if (SP.makeReduced() && SP.solveReduced()) {
		SP.embed();
	} else {
		return false;
	}

	CG.adjAvailable(true);
	copyEmbedding(Gcopy, G, [&origE](adjEntry adj) { return origE.mapEndpoint(adj); });
	for (cluster c : CG.clusters) {
		c->adjEntries.clear();
		for (adjEntry adj : copyC[c]->adjEntries) {
			c->adjEntries.pushBack(origE.mapEndpoint(adj));
		}
	}
	if (m_augmentation) {
		for (auto& pair : *m_augmentation) {
			pair.first = origE.mapEndpoint(pair.first);
			pair.second = origE.mapEndpoint(pair.second);
		}
	}
	return true;
}

namespace ogdf::sync_plan {
using internal::operator<<;

struct FrozenCluster {
	int index = -1, parent = -1, parent_node = -1;
	List<int> nodes;

	FrozenCluster(int _index, int _parent) : index(_index), parent(_parent) { }
};

class UndoInitCluster : public SyncPlan::UndoOperation {
public:
	ClusterGraph* cg;
	List<FrozenCluster> clusters;
	std::vector<std::pair<adjEntry, adjEntry>>* augmentation;

	explicit UndoInitCluster(ClusterGraph* _cg, std::vector<std::pair<adjEntry, adjEntry>>* _aug)
		: cg(_cg), augmentation(_aug) {
		for (cluster c = cg->firstPostOrderCluster(); c != nullptr; c = c->pSucc()) {
			auto it = clusters.emplaceFront(c->index(),
					c->parent() != nullptr ? c->parent()->index() : -1);
			for (node n : c->nodes) {
				(*it).nodes.pushBack(n->index());
			}
		}
	}

	void processCluster(SyncPlan& pq, cluster c, int parent_node, EdgeArray<int>& bicomps) {
		node n = pq.nodeFromIndex(parent_node);
		node t = pq.matchings.getTwin(n);
		pq.log.lout(Logger::Level::Medium)
				<< "Processing cluster " << c << " with node " << pq.fmtPQNode(n, false)
				<< " matched with " << pq.fmtPQNode(t, false) << " in the parent cluster "
				<< c->parent() << std::endl;
		Logger::Indent _(&pq.log);
		pq.log.lout(Logger::Level::Minor) << c->nodes << std::endl;

		PipeBij bij;
		pq.matchings.getIncidentEdgeBijection(t, bij);
		pq.log.lout(Logger::Level::Minor) << printBijection(bij) << std::endl;
		pq.matchings.removeMatching(n, t);
		join(*pq.G, t, n, bij);
		pq.log.lout(Logger::Level::Minor) << printEdges(bij) << std::endl;

		int bc_nr = augmentation != nullptr ? bicomps[bij.front().first] : -1;
		adjEntry pred = nullptr;
		for (const PipeBijPair& pair : bij) {
			adjEntry curr = pair.first->twin();
			c->adjEntries.pushBack(curr);
			if (augmentation != nullptr && bicomps[pair.first] != bc_nr) {
				augmentation->emplace_back(pred, curr);
				bc_nr = bicomps[pair.first];
			}
			pred = curr;
		}
	}

	void undo(SyncPlan& pq) override {
		// OGDF_ASSERT(cg->numberOfClusters() == 1);
		EdgeArray<int> bicomps;
		if (augmentation != nullptr) {
			bicomps.init(cg->constGraph(), -1);
			biconnectedComponents(cg->constGraph(), bicomps);
		}
		cg->rootCluster()->adjEntries.clear();
		ClusterArray<cluster> cluster_index(*cg, nullptr);
		for (cluster c : cg->clusters) {
			cluster_index[c] = c;
		}
		for (FrozenCluster& fc : clusters) {
			cluster c;
			if (fc.index == cg->rootCluster()->index()) {
				OGDF_ASSERT(fc.parent == -1);
				c = cg->rootCluster();
			} else {
				OGDF_ASSERT(fc.parent >= 0);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
				OGDF_ASSERT(cluster_index[fc.parent] != nullptr);
				OGDF_ASSERT(cluster_index[fc.parent]->index() == fc.parent);
				OGDF_ASSERT(cluster_index[fc.index]->index() == fc.index);
				c = cluster_index[fc.index];
#pragma GCC diagnostic pop
				processCluster(pq, c, fc.parent_node, bicomps);
			}
			cluster_index[c] = c;
			for (int n : fc.nodes) {
				cg->reassignNode(pq.nodeFromIndex(n), c);
			}
		}
		cg->adjAvailable(true);
#ifdef OGDF_DEBUG
		for (cluster c = cg->firstPostOrderCluster(); c != cg->rootCluster(); c = c->pSucc()) {
			for (adjEntry adj : c->adjEntries) {
				OGDF_ASSERT(pq.edge_reg[adj] == adj->theEdge());
			}
		}
		cg->constGraph().consistencyCheck();
		cg->consistencyCheck();
		OGDF_ASSERT(cg->representsCombEmbedding());
#endif
	}

	std::ostream& print(std::ostream& os) const override { return os << "UndoInitCluster"; }
};

SyncPlan::SyncPlan(Graph* g, ClusterGraph* cg,
		std::vector<std::pair<adjEntry, adjEntry>>* augmentation, ClusterGraphAttributes* cga)
	: G(g)
	, matchings(G)
	, partitions(G)
	, components(G)
	, GA(cga)
	, is_wheel(*G, false)
#ifdef OGDF_DEBUG
	, consistency(*this)
#endif
{
	OGDF_ASSERT(cg->getGraph() == g);
	undo_stack.pushBack(new ResetIndices(*this));

	if (augmentation != nullptr) {
		augmentation->clear();
	}
	auto* op = new UndoInitCluster(cg, augmentation);
	log.lout() << "Processing " << cg->clusters.size() << " clusters (max id "
			   << cg->maxClusterIndex() << ") from " << cg->firstPostOrderCluster()->index()
			   << " up to, but excluding root " << cg->rootCluster()->index() << "." << std::endl;
	auto fc_it = op->clusters.rbegin();
	for (cluster c = cg->firstPostOrderCluster(); c != cg->rootCluster(); c = c->pSucc()) {
		Logger::Indent _(&log);
		log.lout(Logger::Level::Medium)
				<< "Rerouting perimeter-crossing-edges of cluster " << c->index() << " with parent "
				<< c->parent()->index() << "." << std::endl;

		node cn = G->newNode();
		node pn = G->newNode();
		cg->reassignNode(cn, c);
		cg->reassignNode(pn, c->parent());

		if (GA != nullptr) {
			std::ostringstream ss;
			ss << "CN " << cn->index() << " [" << c->index() << "<" << c->parent()->index() << "]";
			GA->label(cn) = ss.str();
			GA->x(cn) = cga->x(c) + 10;
			GA->y(cn) = cga->y(c) + 10;
			ss.str("");
			ss << "PN " << pn->index() << " [" << c->parent()->index() << ">" << c->index() << "]";
			GA->label(pn) = ss.str();
			GA->x(pn) = cga->x(c) - 10;
			GA->y(pn) = cga->y(c) - 10;
		}

		OGDF_ASSERT(fc_it != op->clusters.rend());
		OGDF_ASSERT((*fc_it).index == c->index());
		(*fc_it).parent_node = cn->index();
		++fc_it;
		log.lout(Logger::Level::Minor)
				<< "Matched child node " << cn->index() << " in cluster " << c->index()
				<< " with parent node " << pn->index() << " in cluster " << c->parent()->index()
				<< ". Now processing " << (c->nodes.size() - 1) << " nodes in child cluster."
				<< std::endl;

		int count = 0;
		for (node n : c->nodes) {
			if (n == cn) {
				continue;
			}
			Logger::Indent _(&log);

			List<adjEntry> adjEntries;
			for (adjEntry adj : n->adjEntries) {
				if (c != cg->clusterOf(adj->twinNode())) {
					adjEntries.pushBack(adj);
				}
			}
			count += adjEntries.size();

			log.lout(Logger::Level::Minor)
					<< "Processing " << n->adjEntries.size() << " incident edges of node "
					<< n->index() << ", of which " << adjEntries.size()
					<< " are perimeter-crossing." << std::endl;
			for (adjEntry adj : adjEntries) {
				splitEdge(*G, adj->twin(), pn, cn);
			}
		}

		if (count == 0) {
			log.lout(Logger::Level::Minor)
					<< "Cluster " << c->index() << " has no perimeter-crossing edges." << std::endl;
		} else {
			G->reverseAdjEdges(pn);
		}

		matchings.matchNodes(cn, pn);
	}
	OGDF_ASSERT(fc_it != op->clusters.rend());
	OGDF_ASSERT((*fc_it).index == cg->rootCluster()->index());
	++fc_it;
	OGDF_ASSERT(fc_it == op->clusters.rend());

	for (cluster c = cg->firstPostOrderCluster(); c != cg->rootCluster(); c = c->pSucc()) {
		while (!c->nodes.empty()) {
			cg->reassignNode(c->nodes.front(), cg->rootCluster());
		}
	}

	initComponents();
	matchings.rebuildHeap();
	pushUndoOperationAndCheck(op);
}

void reduceLevelPlanarityToClusterPlanarity(const Graph& LG,
		const std::vector<std::vector<node>>& emb, Graph& G, ClusterGraph& CG,
		EdgeArray<node>& embMap) {
	NodeArray<std::pair<node, node>> map(LG);
	cluster p = CG.rootCluster();
	for (int l = emb.size() - 1; l >= 0; --l) {
		cluster c = CG.newCluster(p);
		for (int i = 0; i < emb[l].size(); ++i) {
			node n = emb[l][i];
			node u = G.newNode();
			node v = G.newNode();
			CG.reassignNode(u, c);
			CG.reassignNode(v, p);
			map[n] = {u, v};
			embMap[G.newEdge(u, v)] = n;
		}
		p = c;
	}
	for (edge e : LG.edges) {
		G.newEdge(map[e->source()].second, map[e->target()].first);
	}
}

}
