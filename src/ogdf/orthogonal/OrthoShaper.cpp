/** \file
 * \brief Computes the Orthogonal Representation of a Planar
 * Representation of a UML Graph.
 *
 * \author Karsten Klein
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


#include <ogdf/basic/CombinatorialEmbedding.h>
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/Logger.h>
#include <ogdf/basic/SList.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/exceptions.h>
#include <ogdf/graphalg/MinCostFlowReinelt.h>
#include <ogdf/orthogonal/OrthoRep.h>
#include <ogdf/orthogonal/OrthoShaper.h>
#include <ogdf/planarity/PlanRep.h>
#include <ogdf/uml/PlanRepUML.h>

#include <algorithm>
#include <ostream>
#include <string>


const int flowBound = 4; //cant have more than 4 bends in cage boundary, not > 360 degree

enum class NetArcType { defaultArc, angle, backAngle, bend };

namespace ogdf {


//call function: compute a flow in a dual network and interpret
//result as bends and angles (representation shape)
void OrthoShaper::call(PlanRepUML& PG, CombinatorialEmbedding& E, OrthoRep& OR, bool fourPlanar) {
#if 0
	const bool angleMaxBound = true;
#endif
	const bool angleMinBound = false;

	if (PG.numberOfEdges() == 0) {
		return;
	}

	m_fourPlanar = fourPlanar;


	// the min cost flow we use
	MinCostFlowReinelt<int> flowModule;
	const int infinity = flowModule.infinity();


	//fix some values depending on traditional or progressive mode

	//Progressive: Fluss/Winkel Werte:
	//     Grad, v->f, f->v
	//     0  ,  2  ,  0
	//     90 ,  1  ,  0
	//     180,  0  ,  0
	//     270,  0  ,  1
	//     360,  0  ,  2
	//standard flow boundaries for traditional and progressive mode
	const int upperAngleFlow = (m_traditional ? 4 : 1); //non zero
	const int maxAngleFlow = (m_traditional ? 4 : 2); //use 2 for multialign zero degree
	const int maxBackFlow = 2; //maximal flow on back arcs in progressive mode
	const int upperBackAngleFlow = 2; // and 360 back (only progressive mode)
	const int lowerAngleFlow = (m_traditional ? 1 : 0);
	const int piAngleFlow = (m_traditional ? 2 : 0);
	const int halfPiAngleFlow = 1;
#if 0
	const int halfPiBackAngleFlow = 0;  //(only progressive mode)
#endif
	const int zeroAngleFlow = (m_traditional ? 0 : 2);
	const int zeroBackAngleFlow = 0; //(only progressive mode)

	//in progressive mode, angles need cost to work out properly
#if 0
	const int tradAngleCost  = 0;
#endif
	const int progAngleCost = 1;
	const int tradBendCost = 1;
	const int progBendCost = 3 * PG.numberOfNodes(); //should use supply


	OR.init(E);
	FaceArray<node> F(E);

	OGDF_ASSERT(PG.representsCombEmbedding());
	OGDF_ASSERT(F.valid());


	// NETWORK VARIABLES

	Graph Network; //the dual network
	EdgeArray<int> lowerBound(Network, 0); // lower bound for flow
	EdgeArray<int> upperBound(Network, 0); // upper bound for flow

	EdgeArray<int> cost(Network, 0); // cost of an edge
	NodeArray<int> supply(Network, 0); // supply of every node


	//alignment helper
	NodeArray<bool> fixedVal(Network, false); //already set somewhere
	EdgeArray<bool> noBendEdge(Network, false); //for splitter, brother edges etc.

	//NETWORK TO PlanRepUML INFORMATION

	// stores for edges of the Network the corresponding adjEntries
	// nodes, and faces of PG
	EdgeArray<adjEntry> adjCor(Network, nullptr);
	EdgeArray<node> nodeCor(Network, nullptr);
	EdgeArray<face> faceCor(Network, nullptr);

	NodeArray<NetworkNodeType> nodeTypeArray(Network, NetworkNodeType::low);

	//PlanRepUML TO NETWORK INFORMATION

	//Contains for every node of PG the corresponding node in the network
	NodeArray<node> networkNode(PG, nullptr);
	//Contains for every adjEntry of PG the corresponding edge in the network
	AdjEntryArray<edge> backAdjCor(PG, nullptr); //bends
	//contains for every adjEntry of PG the corresponding angle arc in the network
	//note: this doesn't need to correspond to resulting drawing angles
	//bends on the boundary define angles at expanded nodes
	AdjEntryArray<edge> angleArc(PG, nullptr); //angle
	//contains the corresponding back arc face to node in progressive mode
	AdjEntryArray<edge> angleBackArc(PG, nullptr); //angle

	// OTHER INFORMATION

	// Contains for adjacency Entry of PG the face it belongs to in PG
	AdjEntryArray<face> adjF(PG, nullptr);

	//Contains for angle network arc progressive mode backward arc
	EdgeArray<edge> angleTwin(Network, nullptr);

	auto setProgressiveBoundsEqually = [&](edge e, int flow, int flowTwin) {
		upperBound[e] = lowerBound[e] = flow;
		const edge aTwin = angleTwin[e];
		if (aTwin != nullptr) {
			upperBound[aTwin] = lowerBound[aTwin] = flowTwin;
		}
	};
	auto setBoundsEqually = [&](edge e, int flow, int flowTwin) {
		if (m_traditional) {
			upperBound[e] = lowerBound[e] = flow;
		} else {
			setProgressiveBoundsEqually(e, flow, flowTwin);
		}
	};

	//types of network edges, to be used in flow to values
	EdgeArray<NetArcType> l_arcType(Network, NetArcType::angle);

	//contains the outer face
	//face theOuterFace = E.externalFace();

	// GENERATE ALL NODES OF THE NETWORK

	//corresponding to the graphs nodes
	for (node v : PG.nodes) {
		OGDF_ASSERT((!m_fourPlanar) || (v->degree() < 5));

		networkNode[v] = Network.newNode();
		//maybe install a shortcut here for degree 4 nodes if not expanded

		if (v->degree() > 4) {
			nodeTypeArray[networkNode[v]] = NetworkNodeType::high;
		} else {
			nodeTypeArray[networkNode[v]] = NetworkNodeType::low;
		}

		//already set the supply
		if (m_traditional) {
			supply[networkNode[v]] = 4;
		} else {
			supply[networkNode[v]] = 2 * v->degree() - 4;
		}
	}

	//corresponding to the graphs faces
	for (face f : E.faces) {
		F[f] = Network.newNode();

		if (f == E.externalFace()) {
			nodeTypeArray[F[f]] = NetworkNodeType::outer;
			if (m_traditional) {
				supply[F[f]] = -2 * f->size() - 4;
			} else {
				supply[F[f]] = 4;
			}
		} else {
			nodeTypeArray[F[f]] = NetworkNodeType::inner;
			if (m_traditional) {
				supply[F[f]] = -2 * f->size() + 4;
			} else {
				supply[F[f]] = -4;
			}
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	//check the supply sum
	int checksum = 0;
	for (node v : Network.nodes) {
		checksum += supply[v];
	}
	OGDF_ASSERT(checksum == 0);

	for (node v : PG.nodes) {
		Logger::slout() << " v = " << v << " corresponds to " << networkNode[v] << std::endl;
	}
	for (face f : E.faces) {
		Logger::slout() << " face = " << f->index() << " corresponds to " << F[f];
		if (f == E.externalFace()) {
			Logger::slout() << " (Outer Face)";
		}
		Logger::slout() << std::endl;
	}
#endif


	// GENERATE ALL EDGES OF THE NETWORK

	// OPTIMIZATION POTENTIAL:
	// Do not insert edges with upper bound 0 into the network.

	// Locate for every adjacency entry its adjacent faces.
	for (face f : E.faces) {
		for (adjEntry adj : f->entries) {
			adjF[adj] = f;
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	for (face f : E.faces) {
		Logger::slout() << "Face " << f->index() << " : ";
		for (adjEntry adj : f->entries) {
			Logger::slout() << adj << "; ";
		}
		Logger::slout() << std::endl;
	}
#endif

	//check if we can skip the alignment section
	bool skipAlign = true;

	// Insert for every edge the (two) network arcs
	// entering the face nodes, flow defines bends on the edge
	for (edge e : PG.edges) {
		if (PG.typeOf(e) == Graph::EdgeType::generalization) {
			skipAlign = false;
		}
		OGDF_ASSERT(adjF[e->adjSource()]);
		OGDF_ASSERT(adjF[e->adjTarget()]);
		if (F[adjF[e->adjSource()]] != F[adjF[e->adjTarget()]]) {
			// not a selfloop.
			edge newE = Network.newEdge(F[adjF[e->adjSource()]], F[adjF[e->adjTarget()]]);

			l_arcType[newE] = NetArcType::bend;

			adjCor[newE] = e->adjSource();
			if ((PG.typeOf(e) == Graph::EdgeType::generalization)
					|| (PG.isBoundary(e) && (!m_traditional))) {
				upperBound[newE] = 0;
			} else {
				upperBound[newE] = infinity;
			}
			cost[newE] = (m_traditional ? tradBendCost : progBendCost);
#if 0
			cost[newE] = 1;
#endif
			backAdjCor[e->adjSource()] = newE;

			newE = Network.newEdge(F[adjF[e->adjTarget()]], F[adjF[e->adjSource()]]);

			l_arcType[newE] = NetArcType::bend;

			adjCor[newE] = e->adjTarget();
			if ((PG.typeOf(e) == Graph::EdgeType::generalization)
					|| (PG.isBoundary(e) && (m_traditional))) {
				upperBound[newE] = 0;
			} else {
				upperBound[newE] = infinity;
			}
			cost[newE] = (m_traditional ? tradBendCost : progBendCost);
#if 0
			cost[newE] = 1;
#endif
			backAdjCor[e->adjTarget()] = newE;
		}
	}


	// insert for every node edges to all appearances of adjacent faces
	// flow defines angles at nodes
	// progressive: and vice-versa


	// Observe that two generalizations are not allowed to bend on
	// a node. There must be a 180 degree angle between them.

	// assure that there is enough flow between adjacent generalizations
	NodeArray<bool> genshift(PG, false);

	//non-expanded vertex
	for (node v : PG.nodes) {
		// Locate possible adjacent generalizations
		adjEntry gen1 = nullptr;
		adjEntry gen2 = nullptr;

		if (PG.typeOf(v) != Graph::NodeType::generalizationMerger
				&& PG.typeOf(v) != Graph::NodeType::generalizationExpander) {
			for (adjEntry adj : v->adjEntries) {
				if (PG.typeOf(adj->theEdge()) == Graph::EdgeType::generalization) {
					if (!gen1) {
						gen1 = adj;
					} else {
						gen2 = adj;
					}
				}
			}
		}

		for (adjEntry adj : v->adjEntries) {
			edge e2 = Network.newEdge(networkNode[v], F[adjF[adj]]);

			l_arcType[e2] = NetArcType::angle;
			//CHECK bounded edges? and upper == 2 for zero degree
			//progressive and traditional
			upperBound[e2] = upperAngleFlow;
			nodeCor[e2] = v;
			adjCor[e2] = adj;
			faceCor[e2] = adjF[adj];
			angleArc[adj] = e2;

			//do not allow zero degree at non-expanded vertex
			//&& !m_allowLowZero
			//progressive and traditional (compatible)
			if (m_fourPlanar) {
				lowerBound[e2] = lowerAngleFlow; //trad 1 = 90, prog 0 = 180
			}

			//insert opposite arcs face to node in progressive style
			if (!m_traditional) {
				edge e3 = Network.newEdge(F[adjF[adj]], networkNode[v]); //flow for >180 degree

				l_arcType[e3] = NetArcType::backAngle;

				angleTwin[e2] = e3;
				angleTwin[e3] = e2;

				cost[e2] = progAngleCost;
				cost[e3] = progAngleCost;

				lowerBound[e3] = lowerAngleFlow; //180 degree,check highdegree drawings
				upperBound[e3] = upperBackAngleFlow; //infinity;
				//nodeCor   [e3] = v; has no node, is face to node
				adjCor[e3] = adj;
				faceCor[e3] = adjF[adj];
				angleBackArc[adj] = e3;
			}
		}

		//second run to have all angleArcs already initialized
		//set the flow boundaries for special cases
		//association classes
		adjEntry assClassAdj = nullptr;
		for (adjEntry adj : v->adjEntries) {
			//save the entry opposite to an association class
			//(only at the edgeToedge connection node)

			if ((v->degree() != 1) && (PG.isAssClass(adj->theEdge()))) {
				OGDF_ASSERT(assClassAdj == nullptr);
				assClassAdj = adj->cyclicSucc();
			}

			edge e2 = angleArc[adj];

			//check alignment

			if (m_align && !skipAlign) {
				//at generalization, search for connected brother nodes
				if ((PG.alignUpward(adj)) && (PG.isVertex(adj->theNode()))) {
					if (adj == adj->theEdge()->adjSource()) {
						if (PG.typeOf(adj->theEdge()) == Graph::EdgeType::generalization) {
							//search for next real edge entries
							//as this is for non-expanded nodes, we dont need the expansion check
							adjEntry run = adj->faceCycleSucc();
							while ((PG.isExpansion(run->theEdge()))
									&& (PG.typeOf(run->theEdge()) == Graph::EdgeType::generalization)) {
								run = run->faceCycleSucc();
							}
							adjEntry run2 = adj->faceCyclePred();
							while ((PG.isExpansion(run2->theEdge()))
									&& (PG.typeOf(run2->theEdge())
											!= Graph::EdgeType::generalization)) {
								run2 = run2->faceCyclePred();
							}

							if ((PG.alignUpward(run) || PG.alignUpward(run->twin()))
									&& //check for crossings
									(PG.typeOf(run->theEdge()) == Graph::EdgeType::generalization)) {
								if (PG.isBrother(run2->theEdge())) {
									if (m_traditional) {
										// brother?
										lowerBound[e2] = halfPiAngleFlow;
									} else {
										lowerBound[e2] = 0; //brother?
										upperBound[e2] = halfPiAngleFlow;
										edge eA = angleTwin[e2];
										if (eA) {
											lowerBound[eA] = 0;
											upperBound[eA] = 2;
										}
									}
								} else {
									//entweder run2 != adj->theEdge oder
									if (PG.typeOf(run2->theEdge())
											!= Graph::EdgeType::generalization) {
										if (m_traditional) {
											lowerBound[e2] = 2;
										} else {
											// nonbrother has >= 180
											lowerBound[e2] = 0;
											upperBound[e2] = 0;
											edge eA = angleTwin[e2];
											if (eA) {
												lowerBound[eA] = 0;
												upperBound[eA] = upperBackAngleFlow;
											}
										}
									}
								}
								//angles: guarantee lower < upper even if stepwise flow computation
								if (m_traditional) {
									upperBound[e2] = flowBound;
								}

								//next angle entry after adj/e2
								adjEntry nextAE = run2->twin();
								OGDF_ASSERT(nextAE->theNode() == adj->theNode());
								OGDF_ASSERT(nextAE->theNode()->degree() <= 4);
								//genauer: 2 gen 3, 1 gen 4

								//check if next edges are forced/allowed to attach at same side
								//eigentlich: teste ob zwei gen, dann setze  lowerbound
								//entsprechend fuer folgende Kanten bis naechste gen
								if (m_traditional) {
									if (lowerBound[e2] > 1) {
										if (v->degree() > 2) {
											lowerBound[angleArc[nextAE]] = 0;
										} else {
											lowerBound[angleArc[nextAE]] =
													max(0, lowerBound[angleArc[nextAE]] - 1);
										}
									}
									//angles: guarantee lower < upper even if stepwise flow computation
									upperBound[e2] = flowBound;
								} else {
									//there may be brothers on both sides, so allow zero degree
									if (upperBound[e2] == 0) {
										if (v->degree() > 2) {
											lowerBound[angleArc[nextAE]] = 0;
											upperBound[angleArc[nextAE]] = zeroAngleFlow;
											edge eA = angleTwin[e2];
											if (eA) {
												lowerBound[eA] = 0;
												upperBound[eA] = 0;
											}
										} else //adjust this to max above?
										{
											lowerBound[angleArc[nextAE]] = 0;
											upperBound[angleArc[nextAE]] = zeroAngleFlow;
											edge eA = angleTwin[e2];
											if (eA) {
												lowerBound[eA] = 0;
												upperBound[eA] = 0;
											}
										}
									}
								}
								PG.setUserType(adj->theEdge(), 1);
								fixedVal[e2->source()] = true;
							}
						} else {
							//from left side to gen
							//search for next real edge entries
							adjEntry run2 = adj->faceCyclePred();
							while ((PG.isExpansion(run2->theEdge()))
									&& (PG.typeOf(run2->theEdge())
											!= Graph::EdgeType::generalization)) {
								run2 = run2->faceCyclePred(); //is vertex, no iteration!?
							}

							if (PG.alignUpward(run2) && //check for crossings
									(PG.typeOf(adj->faceCyclePred()->theEdge())
											== Graph::EdgeType::generalization)) {
								//check if min 90 (brother) or 180 degree
								if (m_traditional) {
									if (PG.isBrother(adj->theEdge())) {
										lowerBound[e2] = 1;
									} else {
										lowerBound[e2] = 2; //2; teste 1
									}
									//angles: guarantee lower < upper even if stepwise flow computation
									upperBound[e2] = flowBound;
								} else {
									lowerBound[e2] = 0;
									edge eA = angleTwin[e2];
									if (eA) {
										lowerBound[eA] = 0;
										upperBound[eA] = upperBackAngleFlow;
									}
									//90 degree min
									if (PG.isBrother(adj->theEdge())) {
										upperBound[e2] = halfPiAngleFlow;
									}
									//180 degree min
									else {
										upperBound[e2] = 0;
									}
								}

								OGDF_ASSERT(lowerBound[e2] <= upperBound[e2]);
								adjEntry nextAE = adj->twin()->faceCycleSucc();
								OGDF_ASSERT(nextAE->theNode() == v);

								//eigentlich: teste ob zwei gen, dann setze  lowerbound
								//entsprechend fuer folgende Kanten bis naechste gen
								if (m_traditional) {
									lowerBound[angleArc[nextAE]] = 0;
								} else {
									lowerBound[angleArc[nextAE]] = 0;
									upperBound[angleArc[nextAE]] = maxAngleFlow;
									edge eA = angleTwin[angleArc[nextAE]];
									if (eA) {
										lowerBound[eA] = 0;
										upperBound[eA] = upperBackAngleFlow;
									}
								}

								PG.setUserType(adj->theEdge(), 1);
								fixedVal[e2->source()] = true;
							}
						}
					}
				} else {
					//search backwards for non-brother edges in hierarchies
					//first guarantee that this is only a non-expanded vertex
					if (PG.isVertex(adj->theNode())
							// outer gen right of hierarchy level
							&& PG.typeOf(adj->theEdge()) != Graph::EdgeType::generalization
							&& !PG.isExpansion(adj->theEdge())) {
						adjEntry run2 = adj->faceCyclePred();
						while (PG.isExpansion(run2->theEdge())
								&& PG.typeOf(run2->theEdge()) != Graph::EdgeType::generalization) {
							run2 = run2->faceCyclePred();
						}

						// is this a gen to a merger
						if (PG.alignUpward(run2->twin()) //check for crossings
								&& PG.typeOf(run2->theEdge()) == Graph::EdgeType::generalization
								&& run2 == run2->theEdge()->adjTarget()) {
							adjEntry run = run2->faceCyclePred();
							while (PG.isExpansion(run->theEdge())
									&& PG.typeOf(run->theEdge()) == Graph::EdgeType::generalization) {
								run = run->faceCyclePred();
							}

							if (PG.alignUpward(run) && PG.isGeneralization(run->theEdge())
									&& run == run->theEdge()->adjSource()) {
								if (m_traditional) {
									if (PG.isBrother(adj->theEdge())) {
										lowerBound[e2] = 1;
									} else {
										lowerBound[e2] = 2; //2; teste 1
									}
									//angles: guarantee lower < upper even if stepwise flow computation
									upperBound[e2] = flowBound;
								} else {
									if (PG.isBrother(adj->theEdge())) {
										setAngleBound(e2, 90, lowerBound, upperBound, angleTwin,
												angleMinBound);
									} else {
										setAngleBound(e2, 180, lowerBound, upperBound, angleTwin,
												angleMinBound);
									}
								}

								//relax next entries angle
								adjEntry nextAE = adj->cyclicPred();
								//eigentlich: teste ob zwei gen, dann setze  lowerbound
								//entsprechend fuer folgende Kanten bis naechste gen
								if (m_traditional) {
									lowerBound[angleArc[nextAE]] = 0;
								} else {
									setAngleBound(angleArc[nextAE], 0, lowerBound, upperBound,
											angleTwin, angleMinBound);
								}

								PG.setUserType(adj->theEdge(), 1);
								fixedVal[e2->source()] = true;
							}
						}
					}
				}
			}

			//hier muss man fuer die Kanten, die rechts ansetzen noch lowerbound 2 setzen

			// NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): adj can never be nullptr
			if ((gen2 == adj && gen1 == adj->cyclicSucc())
					|| (gen1 == adj && gen2 == adj->cyclicSucc())) {
				setBoundsEqually(e2, piAngleFlow, 0);
				genshift[v] = true;
			}
		}
		//process special case of association classes: 180 degree angle
		if (assClassAdj != nullptr) {
			edge e2 = angleArc[assClassAdj];
			lowerBound[e2] = piAngleFlow;

			if (!m_traditional) {
				edge e3 = angleTwin[e2];
				upperBound[e3] = lowerBound[e3] = 0;
			}
		}
	}

	// Reset upper and lower Bounds for network arcs that
	// correspond to edges of generalizationmerger faces
	// and edges of expanded nodes.

	for (node v : PG.nodes) {
		if (PG.expandAdj(v)) {
			// Get the corresponding face in the original embedding.
			face f = adjF[PG.expandAdj(v)];

			//expanded merger cages
			if (PG.typeOf(v) == Graph::NodeType::generalizationMerger) {
				// Set upperBound to 0  for all edges.
				for (adjEntry adj : f->entries) {
					//no bends on boundary (except special case following)
					upperBound[backAdjCor[adj]] = 0;
					upperBound[backAdjCor[adj->twin()]] = 0;

					// Node w is in Network
					node w = networkNode[adj->twinNode()];
					for (adjEntry adjW : w->adjEntries) {
						edge e = adjW->theEdge();
						if (e->target() == F[f]) {
							// is this: 180 degree?
							// traditional: 2 progressive: 0
							// if not traditional limit angle back arc
							setBoundsEqually(e, piAngleFlow, 0);
						}
					}
				}
				//special bend case
				// Set the upper and lower bound for the first edge of
				// the mergeexpander face to guarantee a 90 degree bend.
				if (m_traditional) {
					upperBound[backAdjCor[PG.expandAdj(v)]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)]] = 1;
				} else {
					//progressive mode: bends are in opposite direction
					upperBound[backAdjCor[PG.expandAdj(v)->twin()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->twin()]] = 1;
				}

				// Set the upper and lower bound for the first node in
				// clockwise order of the mergeexpander face to
				// guaranty a 90 degree angle at the node in the interior
				// and a 180 degree angle between the generalizations in the
				// exterior.
				node secFace;

				if (F[f] == backAdjCor[PG.expandAdj(v)]->target()) {
					secFace = backAdjCor[PG.expandAdj(v)]->source();
				} else {
					OGDF_ASSERT(F[f] == backAdjCor[PG.expandAdj(v)]->source());
					secFace = backAdjCor[PG.expandAdj(v)]->target();
				}

				node w = networkNode[PG.expandAdj(v)->twinNode()];

				adjEntry adjFound = nullptr;
				for (adjEntry adj : w->adjEntries) {
					if (adj->theEdge()->target() == F[f]) {
						// if not traditional limit angle back arc
						setBoundsEqually(adj->theEdge(), 1, 0);
						adjFound = adj;
						break;
					}
				}
				OGDF_ASSERT(adjFound != nullptr);

				edge e;
				if (m_traditional) {
					e = adjFound->cyclicSucc()->theEdge();
				} else {
					//we have two edges instead of one per face
					adjEntry ae = adjFound->cyclicSucc();
					e = ae->theEdge();
					if (e->target() != secFace) {
						//maybe we have to jump one step further
						e = ae->cyclicSucc()->theEdge();
					}
				}

				if (e->target() == secFace) {
					setBoundsEqually(e, piAngleFlow, piAngleFlow);
				}

				// Set the upper and lower bound for the last edge of
				// the mergeexpander face to guarantee a 90 degree bend.
				if (m_traditional) {
					upperBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()]] = 1;
				} else {
					//progressive mode: bends are in opposite direction
					upperBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()->twin()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()->twin()]] = 1;
				}

				// Set the upper and lower bound for the last node in
				// clockwise order of the mergeexpander face to
				// guaranty a 90 degree angle at the node in the interior
				// and a 180 degree angle between the generalizations in the
				// exterior.
				if (F[f] == backAdjCor[PG.expandAdj(v)->faceCyclePred()]->target()) {
					secFace = backAdjCor[PG.expandAdj(v)->faceCyclePred()]->source();
				} else {
					OGDF_ASSERT(F[f] == backAdjCor[PG.expandAdj(v)->faceCyclePred()]->source());
					secFace = backAdjCor[PG.expandAdj(v)->faceCyclePred()]->target();
				}

				w = networkNode[PG.expandAdj(v)->faceCyclePred()->theNode()];

				adjFound = nullptr;
				for (adjEntry adj : w->adjEntries) {
					if (adj->theEdge()->target() == F[f]) {
						// if not traditional limit angle back arc
						setBoundsEqually(adj->theEdge(), 1, 0);
						adjFound = adj;
						break;
					}
				}
				OGDF_ASSERT(adjFound);

				if (m_traditional) {
					e = adjFound->cyclicPred()->theEdge();
				} else {
					//we have two edges instead of one per face
					adjEntry ae = adjFound->cyclicPred();
					e = ae->theEdge();
					if (e->target() != secFace) {
						//maybe we have to jump one step further
						e = ae->cyclicPred()->theEdge();
					}
				}

				if (e->target() == secFace) {
					setBoundsEqually(e, piAngleFlow, piAngleFlow);
				}
			}

			//expanded high degree cages
			else if (PG.typeOf(v) == Graph::NodeType::highDegreeExpander) {
				if (m_align && !skipAlign) {
					adjEntry splitter = nullptr;
					face expansionFace = f;
#ifdef OGDF_DEBUG
					int bendCount = 0; //check bend maximum in face
#endif

					do {
						//this double iteration slows the algorithm down
						for (adjEntry adj : expansionFace->entries) {
							if (!PG.faceSplitter(adj->theEdge())) {
								//right from generalization
								adjEntry srcadj = adj->cyclicPred();
								adjEntry tgtadj = adj->twin()->cyclicSucc();
								//set min 90 degree if brother edge
								if (PG.isBrother(tgtadj->theEdge())) {
									if (PG.isGeneralization(srcadj->theEdge())
											&& (srcadj == srcadj->theEdge()->adjSource())) {
#ifdef OGDF_DEBUG
										bendCount++;
#endif
										if (m_traditional) {
											lowerBound[backAdjCor[adj]] = 1;
										}

										//progressive CHECK
										else {
											lowerBound[backAdjCor[adj->twin()]] = 1;
										}

										//maybe set upperBound to avoid conflict
#if 0
										upperBound[backAdjCor[adj]] = flowBound;
											max(1, upperBound[backAdjCor[adj]]);
#endif
									}

									noBendEdge[backAdjCor[tgtadj]] = true;
								} else {
									//hier: falls brother src, gen tgt
									//nonbrother src, gn tgt

									//non-brothers start from lower node side
									if (PG.isGeneralization(srcadj->theEdge())
											&& (srcadj == srcadj->theEdge()->adjSource())
											&& PG.alignUpward(srcadj) //edge to merger
									) {
#ifdef OGDF_DEBUG
										bendCount += 2;
#endif
										if (m_traditional) {
											lowerBound[backAdjCor[adj]] = 2;
											//angles: guarantee lower < upper even if stepwise flow computation
											upperBound[backAdjCor[adj]] = flowBound;
										} else {
											lowerBound[backAdjCor[adj->twin()]] = 2;
											//angles: guarantee lower < upper even if stepwise flow computation
											upperBound[backAdjCor[adj->twin()]] = flowBound;
										}
									} else { // check if we are left from gen
										if (PG.isGeneralization(tgtadj->theEdge())
												&& (tgtadj == tgtadj->theEdge()->adjSource())
												//recent test change, maybe delete again
												&& PG.alignUpward(tgtadj)) {
											if (PG.isBrother(srcadj->theEdge())) {
#ifdef OGDF_DEBUG
												bendCount++;
#endif
												if (m_traditional) {
													lowerBound[backAdjCor[adj]] = 1;
												} else {
													lowerBound[backAdjCor[adj->twin()]] = 1;
												}
												noBendEdge[backAdjCor[srcadj]] = true;
											} else {
#ifdef OGDF_DEBUG
												bendCount += 2;
#endif
												if (m_traditional) {
													lowerBound[backAdjCor[adj]] = 2;
													upperBound[backAdjCor[adj]] = flowBound;
												} else {
													lowerBound[backAdjCor[adj->twin()]] = 2;
													upperBound[backAdjCor[adj->twin()]] = flowBound;
												}
											}
										}
									}
								}
							} else {
								splitter = adj;
							}
						}
						if (splitter && (expansionFace == f)) {
							adjEntry adj = splitter->twin();
							// Get the corresponding face in the original embedding.
							expansionFace = adjF[adj];
							splitter = splitter->twin();
						} else {
							expansionFace = nullptr;
						}
					} while (expansionFace);
					OGDF_ASSERT(bendCount <= 4);
				}

				// Set upperBound to 1 for all edges, allowing maximal one
				// 90 degree bend.
				// Set upperBound to 0 for the corresponding entering edge
				// allowing no 270 degree bend.
				// Set upperbound to 1 for every edge corresponding to the
				// angle of a vertex. This permitts 270 degree angles in
				// the face

				adjEntry splitter = nullptr;


				//assure that edges are only spread around the sides if not too
				//many multi edges are aligned

				//count multiedges at node
				int multis = 0;
				AdjEntryArray<bool> isMulti(PG, false);
				if (m_multiAlign) {
					//if all edges are multi edges, find a 360 degree position
					bool allMulti = true;
					for (adjEntry adj : f->entries) //this double iteration slows the algorithm down
					{
						if (!PG.faceSplitter(adj->theEdge())) {
							adjEntry srcadj = adj->cyclicPred();
							adjEntry tgtadj = adj->twin()->cyclicSucc();
							//check if the nodes are expanded
							node vt1, vt2;
							if (PG.expandedNode(srcadj->twinNode())) {
								vt1 = PG.expandedNode(srcadj->twinNode());
							} else {
								vt1 = srcadj->twinNode();
							}
							if (PG.expandedNode(tgtadj->twinNode())) {
								vt2 = PG.expandedNode(tgtadj->twinNode());
							} else {
								vt2 = tgtadj->twinNode();
							}
							if (vt1 == vt2) {
								//we forbid bends between two incident multiedges
								if (m_traditional) {
									lowerBound[backAdjCor[adj]] = upperBound[backAdjCor[adj]] = 0;
									isMulti[adj] = true;
								} else {
									lowerBound[backAdjCor[adj->twin()]] =
											lowerBound[backAdjCor[adj]] =
													upperBound[backAdjCor[adj]] =
															upperBound[backAdjCor[adj->twin()]] = 0;
									isMulti[adj->twin()] = true;
								}
								multis++;
							} else {
								allMulti = false;
							}
						}
					}
					//multi edge correction: only multi edges => one edge needs 360 degree
					if (allMulti) {
						//find an edge that allows 360 degree without bends
						bool twoNodeCC = true; //no foreign non-multi edge to check for
						for (adjEntry adj : f->entries) {
							//now check for expanded nodes
							adjEntry adjOut = adj->cyclicPred(); //outgoing edge entry
							node vOpp = adjOut->twinNode();
							if (PG.expandedNode(vOpp)) {
								adjOut = adjOut->faceCycleSucc(); //on expanded boundary
								//does not end on self loops
								node vStop = vOpp;
								if (PG.expandedNode(vStop)) {
									vStop = PG.expandedNode(vStop);
								}
								while (PG.expandedNode(adjOut->twinNode()) == vStop) {
									//we are still on vOpps cage
									adjOut = adjOut->faceCycleSucc();
								}
							}
							//now adjOut is either a "foreign" edge or one of the
							//original multi edges if two-node-CC
							//adjEntry testadj = adjCor[e]->faceCycleSucc()->twin();
							adjEntry testAdj = adjOut->twin();
							node vBack = testAdj->theNode();
							if (PG.expandedNode(vBack)) {
								vBack = PG.expandedNode(vBack);
							}
							if (vBack != v) //v is expanded node
							{
								//dont use iteration result, set firstedge!
								upperBound[backAdjCor[adj]] = 4; //4 bends for 360
								twoNodeCC = false;
								break;
							}
						}
						//if only two nodes with multiedges are in current CC,
						//assign 360 degree to first edge
						if (twoNodeCC) {
							//it would be difficult to guarantee that the networkedge
							//on the other side of the face would get the 360, so alllow
							//360 for all edges or search for the outer face

							for (adjEntry adj : f->entries) {
								adjEntry ae = adj->cyclicPred();
								if (adjF[ae] == E.externalFace()) {
									// 4 bends for 360
									upperBound[backAdjCor[adj]] = 4;
									break;
								}
							}
						}
					}
					//End multi edge correction
				}

				//now set the upper Bounds
				for (adjEntry adj : f->entries) {
					//should be: no 270 degrees
					if (m_traditional) {
						upperBound[backAdjCor[adj->twin()]] = 0;
					} else {
						upperBound[backAdjCor[adj]] = 0;
					}

					if (PG.faceSplitter(adj->theEdge())) {
						// No bends allowed  on the face splitter
						upperBound[backAdjCor[adj]] = 0;

						//CHECK
						//progressive??? sollte sein:
						upperBound[backAdjCor[adj->twin()]] = 0;

						splitter = adj;
						continue;
					} else
					//should be: only one bend
					{
						if (m_distributeEdges)
						//CHECK
						//maybe we should change this to setting the lower bound too,
						//depending on the degree (<= 4 => deg 90)
						{
							//check the special case degree >=4 with 2
							// generalizations following each other if degree
							// > 4, only 90 degree allowed, nodeType high
							// bloed, da nicht original
							//if (nodeTypeArray[ networkNode[adj->twinNode()] ] == high)
							//hopefully size is original degree
							if (m_traditional) {
								if (!isMulti[adj]) //m_multiAlign???
								{
									//check if original node degree minus multi edges
									//is high enough
									//Attention: There are some lowerBounds > 1
#ifdef OGDF_DEBUG
									int oldBound =
#endif
											upperBound[backAdjCor[adj]];
									if ((!genshift[v]) && (f->size() - multis > 3)) {
										upperBound[backAdjCor[adj]] =
												max(1, lowerBound[backAdjCor[adj]]);
										// max(2, lowerBound[backAdjCor[adj]]);
										// due to mincostflowreinelt errors, we are not
										// allowed to set ub 1
									} else {
										upperBound[backAdjCor[adj]] =
												max(2, lowerBound[backAdjCor[adj]]);
									}
									//nur zum Testen der Faelle
									OGDF_ASSERT(oldBound >= upperBound[backAdjCor[adj]]);
								}
							} else {
								//preliminary set the bound in all cases

								if (!isMulti[adj]) //m_multiAlign???
								{
									//Attention: There are some lowerBounds > 1
									if ((!genshift[v]) && (f->size() - multis > 3)) {
										upperBound[backAdjCor[adj->twin()]] =
												max(1, lowerBound[backAdjCor[adj->twin()]]);
									} else {
										upperBound[backAdjCor[adj->twin()]] =
												max(2, lowerBound[backAdjCor[adj->twin()]]);
									}
								}
							}
						}
					}

					// Node w is in Network
					node w = networkNode[adj->twinNode()];

#if 0
					if (w && !(m_traditional && m_fourPlanar && (w->degree() != 4)))
#endif
					{
						//should be: inner face angles set to 180
						for (adjEntry adjW : w->adjEntries) {
							edge e = adjW->theEdge();
							if (e->target() == F[f]) {
								setBoundsEqually(e, piAngleFlow, piAngleFlow);
							}
						}
					}
				}

				// In case a face splitter was used, we need to update the
				// second face of the cage.
				if (splitter) {
					// Get the corresponding face in the original embedding.
					face f2 = adjF[splitter->twin()];

					for (adjEntry adj : f2->entries) {
						if (adj == splitter->twin()) {
							continue;
						}

						//todo: Alignment

						if (m_traditional) {
							upperBound[backAdjCor[adj->twin()]] = 0;
						} else { //progressive bends are in opposite direction
							upperBound[backAdjCor[adj]] = 0;
						}

						// Node w is in Network
						node w = networkNode[adj->twinNode()];
						// if (w && !(m_traditional && m_fourPlanar && (w->degree() != 4)))
						{
							for (adjEntry adjW : w->adjEntries) {
								edge e = adjW->theEdge();
								if (e->target() == F[f2]) {
									setBoundsEqually(e, piAngleFlow, piAngleFlow);
								}
							}
						}
					}
				}
			}
		} else {
			//non-expanded (low degree) nodes
			//check for alignment and for multi edges

			//check for multi edges and decrease lowerbound if align
			//int lowerb = 0;

			if (PG.isVertex(v)) {
				node w = networkNode[v];
				if ((nodeTypeArray[w] != NetworkNodeType::low) || (w->degree() < 2)) {
					continue;
				}

				bool allMulti = true;
				for (adjEntry adj : w->adjEntries) {
					edge e = adj->theEdge();
					//lowerb += max(lowerBound[e], 0);

					OGDF_ASSERT((!m_traditional) || (e->source() == w));
					if (m_traditional && (e->source() != w)) {
						OGDF_THROW(AlgorithmFailureException);
					}
					if (e->source() != w) {
						continue; //dont treat back angle edges
					}

					if (m_multiAlign && (v->degree() > 1)) {
						adjEntry srcAdj = adjCor[e];
						adjEntry tgtAdj = adjCor[e]->faceCyclePred();

						//check if the nodes are expanded
						node vt1, vt2;
						if (PG.expandedNode(srcAdj->twinNode())) {
							vt1 = PG.expandedNode(srcAdj->twinNode());
						} else {
							vt1 = srcAdj->twinNode();
						}

						if (PG.expandedNode(tgtAdj->theNode())) {
							vt2 = PG.expandedNode(tgtAdj->theNode());
						} else {
							vt2 = tgtAdj->theNode();
						}

						if (vt1 == vt2) {
							fixedVal[w] = true;

							//we forbid bends between incident multi edges
							//or is it angle?
							setBoundsEqually(e, zeroAngleFlow, zeroBackAngleFlow);
						} else {
							//CHECK
							//to be done: only if multiedges
							if (!genshift[v]) {
								upperBound[e] = upperAngleFlow;
							}
							allMulti = false;
						}
					}
				}

				if (m_multiAlign && allMulti && (v->degree() > 1)) {
					fixedVal[w] = true;

					//find an edge that allows 360 degree without bends
					bool twoNodeCC = true;
					for (adjEntry adj : w->adjEntries) {
						edge e = adj->theEdge();
#if 0
						if (PG.expandedNode(srcAdj->twinNode())) {
							vt1 = PG.expandedNode(srcAdj->twinNode());
						}
#endif
						//now check for expanded nodes
						adjEntry runAdj = adjCor[e];
						node vOpp = runAdj->twinNode();
						node vStop;
						vStop = vOpp;
						runAdj = runAdj->faceCycleSucc();
						if (PG.expandedNode(vStop)) {
							//does not end on self loops
							vStop = PG.expandedNode(vStop);
							while (PG.expandedNode(runAdj->twinNode()) == vStop) {
								//we are still on vOpps cage
								runAdj = runAdj->faceCycleSucc();
							}
						}
						//adjEntry testadj = adjCor[e]->faceCycleSucc()->twin();
						adjEntry testAdj = runAdj->twin();
						node vBack = testAdj->theNode();

						if (vBack != v) //not same node
						{
							if (PG.expandedNode(vBack)) {
								vBack = PG.expandedNode(vBack);
							}
							if (vBack != vStop) //vstop !=0, not inner face in 2nodeCC
							{
								//CHECK: 4? upper
								OGDF_ASSERT(!PG.expandedNode(v)); //otherwise not angle flow
								if (m_traditional) {
									// dont use iteration result, set firstedge!
									upperBound[e] = maxAngleFlow;
								} else {
									setProgressiveBoundsEqually(e, lowerAngleFlow, maxBackFlow);
								}
								twoNodeCC = false;
								break;
							}
						}
					}
					//if only two nodes with multiedges are in current CC,
					//assign 360 degree to first edge
					if (twoNodeCC) {
						//it would be difficult to guarantee that the networkedge
						//on the other side of the face would get the 360, so allow
						//360 for all edges or search for external face
						for (adjEntry adj : w->adjEntries) {
							edge e = adj->theEdge();
							adjEntry adje = adjCor[e];
							if (adjF[adje] == E.externalFace()) {
								//CHECK: 4? upper
								OGDF_ASSERT(!PG.expandedNode(v)); //otherwise not angle flow
								if (m_traditional) {
									upperBound[e] = maxAngleFlow; //upperAngleFlow;
								} else {
									setProgressiveBoundsEqually(e, lowerAngleFlow, maxBackFlow);
								}
								break;
							}
						}
					}
				}
			}
		}
	}

	//int flowSum = 0;

	//To Be done: hier multiedges testen
	for (node tv : Network.nodes) {
		//flowSum += supply[tv];

		//only check representants of original nodes, not faces
		if (nodeTypeArray[tv] == NetworkNodeType::low || nodeTypeArray[tv] == NetworkNodeType::high) {
			//if node representant with degree 4, set angles preliminary
			//degree four nodes with two gens are expanded in PlanRepUML
			//all others are allowed to change the edge positions
			if ((m_traditional && (tv->degree() == 4)) || ((tv->degree() == 8) && !m_traditional)) {
				//three types: degree4 original nodes and facesplitter end nodes,
				//maybe crossings
				//fixassignment tells us that low degree nodes are not allowed to
				//have zero degree and special nodes are already assigned
				bool fixAssignment = true;

				//check if free assignment is possible for degree 4
				if (m_deg4free) {
					fixAssignment = false;
					for (adjEntry adj : tv->adjEntries) {
						edge te = adj->theEdge();
						if (te->source() == tv) {
							adjEntry pgEntry = adjCor[te];
							node pgNode = pgEntry->theNode();

							if ((PG.expandedNode(pgNode)) || (PG.faceSplitter(adjCor[te]->theEdge()))
									|| (PG.typeOf(pgNode) == Graph::NodeType::dummy) //test crossings
							) {
								fixAssignment = true;
								break;
							}
						}
					}
				}

				//CHECK
				//now set the angles at degree 4 nodes to distribute edges
				for (adjEntry adj : tv->adjEntries) {
					edge te = adj->theEdge();

					if (te->source() == tv) {
						if (fixedVal[tv]) {
							continue; //if already special values set
						}

						if (!fixAssignment) {
							lowerBound[te] = 0; //lowerAngleFlow maybe 1;//0;
							upperBound[te] = upperAngleFlow; //4;
						} else {
							//only allow 90 degree arc value
							lowerBound[te] = halfPiAngleFlow;
							upperBound[te] = halfPiAngleFlow;
						}
					} else {
						if (fixedVal[tv]) {
							continue; //if already special values set
						}

						if (!fixAssignment) {
							OGDF_ASSERT(lowerAngleFlow == 0); //should only be in progressive mode
							lowerBound[te] = lowerAngleFlow;
							upperBound[te] = upperBackAngleFlow;
						} else {
							//only allow 0-180 degree back arc value
							lowerBound[te] = 0; //1
							upperBound[te] = 0; //halfPiAngleFlow; //1
						}
					}
				}
			}
#ifdef OGDF_DEBUG
			int lowsum = 0, upsum = 0;
#endif
			for (adjEntry adj : tv->adjEntries) {
				edge te = adj->theEdge();
				OGDF_ASSERT(lowerBound[te] <= upperBound[te]);
				if (noBendEdge[te]) {
					lowerBound[te] = 0;
				}
#ifdef OGDF_DEBUG
				lowsum += lowerBound[te];
				upsum += upperBound[te];
#endif
			}
			if (m_traditional) {
				OGDF_ASSERT(lowsum <= supply[tv]);
				OGDF_ASSERT(upsum >= supply[tv]);
			}
		}
	}
	for (node tv : Network.nodes) {
		for (adjEntry adj : tv->adjEntries) {
			edge te = adj->theEdge();
			if (noBendEdge[te]) {
				lowerBound[te] = 0;
			}
		}
	}

	bool isFlow = false;
	SList<edge> capacityBoundedEdges;
	EdgeArray<int> flow(Network, 0);

	// Set upper Bound. Do not leave it to INT_NAX.
	// Causes problems with MinCostFlowReinelt

	//but some edges are no longer capacitybounded, therefore save their status
	EdgeArray<bool> isBounded(Network, false);

	for (edge e : Network.edges) {
		if (upperBound[e] == infinity) {
			capacityBoundedEdges.pushBack(e);
			isBounded[e] = true;
		}
	}

	int currentUpperBound;
	if (m_startBoundBendsPerEdge > 0) {
		currentUpperBound = m_startBoundBendsPerEdge;
	} else {
		currentUpperBound = 4 * PG.numberOfEdges();
	}

	while ((!isFlow) && (currentUpperBound <= 4 * PG.numberOfEdges())) {
		for (edge ei : capacityBoundedEdges) {
			upperBound[ei] = currentUpperBound;
		}

		isFlow = flowModule.call(Network, lowerBound, upperBound, cost, supply, flow);

#if 0
		if (isFlow)
		{
#	if 0
			if (int(ogdf::debugLevel) >= int(dlHeavyChecks))
#	endif
			{
				for(edge e : Network.edges) {
					fout << "e = " << e << " flow = " << flow[e];
					if(nodeCor[e] == 0 && adjCor[e])
						fout << " real edge = " << adjCor[e]->theEdge();
					fout << std::endl;
				}
				for(edge e : Network.edges) {
					if(nodeCor[e] == 0 && adjCor[e] != 0 && flow[e] > 0) {
						fout << "Bends " << flow[e] << " on edge "
							<< adjCor[e]->theEdge()
							<< " between faces " << adjF[adjCor[e]]->index() << " - "
							<< adjF[adjCor[e]->twin()]->index() << std::endl;
					}
				}
				for(edge e : Network.edges) {
					if(nodeCor[e] != 0 && faceCor[e] != 0) {
						fout << "Angle " << (flow[e])*90 << "\tdegree   on node "
							<< nodeCor[e] << " at face " << faceCor[e]->index()
							<< "\tbetween edge " << adjCor[e]->faceCyclePred()
							<< "\tand " << adjCor[e] << std::endl;
					}
				}
				if (startBoundBendsPerEdge> 0) {
					fout << "Minimizing edge bends for upper bound "
						<< currentUpperBound;
					if(isFlow) {
						fout << " ... Successful";
					}
					fout << std::endl;
				}
			}
		}
#endif

		OGDF_ASSERT(m_startBoundBendsPerEdge >= 1 || isFlow);

		currentUpperBound++;
	}

	if (m_startBoundBendsPerEdge && !isFlow) {
		OGDF_THROW_PARAM(AlgorithmFailureException, AlgorithmFailureCode::NoFlow);
	}

#ifdef OGDF_HEAVY_DEBUG
	int totalNumBends = 0;
#endif

	//int gap = currentUpperBound;

	for (edge e : Network.edges) {
		if (nodeCor[e] == nullptr && adjCor[e] != nullptr && (flow[e] > 0)
				&& (angleTwin[e] == nullptr)) //no angle edges
		{
			OGDF_ASSERT(OR.bend(adjCor[e]).size() == 0);

			char zeroChar = (m_traditional ? '0' : '1');
			char oneChar = (m_traditional ? '1' : '0');
			//we depend on the property that there is no flow
			//in opposite direction due to the cost
			OR.bend(adjCor[e]).set(zeroChar, flow[e]);
			OR.bend(adjCor[e]->twin()).set(oneChar, flow[e]);

#ifdef OGDF_HEAVY_DEBUG
			totalNumBends += flow[e];
#endif

#if 0
			//check if bends fit bounds
			if (isBounded[e])
			{
				OGDF_ASSERT((int)OR.bend(adjCor[e]).size() <= currentUpperBound);
				OGDF_ASSERT((int)OR.bend(adjCor[e]->twin()).size() <= currentUpperBound);
			}
#endif
		} else if (nodeCor[e] != nullptr && faceCor[e] != nullptr) {
			if (m_traditional) {
				OR.angle(adjCor[e]) = (flow[e]);
			} else {
				OGDF_ASSERT(angleTwin[e]);
				OGDF_ASSERT(flow[e] >= 0);
				OGDF_ASSERT(flow[e] <= 2);

				const int twinFlow = flow[angleTwin[e]];

				if (flow[e] == 0) {
					OGDF_ASSERT(twinFlow >= 0);
					OGDF_ASSERT(twinFlow <= 2);

					OR.angle(adjCor[e]) = 2 + twinFlow;
				} else {
					OGDF_ASSERT(twinFlow == 0);
					OR.angle(adjCor[e]) = 2 - flow[e];
				}
			}
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	Logger::slout() << "\n\nTotal Number of Bends : " << totalNumBends << std::endl << std::endl;

	string error;
	if (!OR.check(error)) {
		Logger::slout() << error << std::endl;
		OGDF_ASSERT(false);
	}
#endif
}

//call function: compute a flow in a dual network and interpret
//result as bends and angles (representation shape)
void OrthoShaper::call(PlanRep& PG, CombinatorialEmbedding& E, OrthoRep& OR, bool fourPlanar) {
#if 0
	const bool angleMaxBound = true;
	const bool angleMinBound = false;
#endif

	if (PG.numberOfEdges() == 0) {
		return;
	}

	m_fourPlanar = fourPlanar;


	// the min cost flow we use
	MinCostFlowReinelt<int> flowModule;
	const int infinity = flowModule.infinity();


	//fix some values depending on traditional or progressive mode

	//Progressive: Fluss/Winkel Werte:
	//     Grad, v->f, f->v
	//     0  ,  2  ,  0
	//     90 ,  1  ,  0
	//     180,  0  ,  0
	//     270,  0  ,  1
	//     360,  0  ,  2
	//standard flow boundaries for traditional and progressive mode
	const int upperAngleFlow = (m_traditional ? 4 : 1); //non zero
	const int maxAngleFlow = (m_traditional ? 4 : 2); //use 2 for multialign zero degree
	const int maxBackFlow = 2; //maximal flow on back arcs in progressive mode
	const int upperBackAngleFlow = 2; // and 360 back (only progressive mode)
	const int lowerAngleFlow = (m_traditional ? 1 : 0);
	const int piAngleFlow = (m_traditional ? 2 : 0);
	const int halfPiAngleFlow = 1;
#if 0
	const int halfPiBackAngleFlow = 0;  //(only progressive mode)
#endif
	const int zeroAngleFlow = (m_traditional ? 0 : 2);
	const int zeroBackAngleFlow = 0; //(only progressive mode)

	//in progressive mode, angles need cost to work out properly
#if 0
	const int tradAngleCost  = 0;
#endif
	const int progAngleCost = 1;
	const int tradBendCost = 1;
	const int progBendCost = 3 * PG.numberOfNodes(); //should use supply


	OR.init(E);
	FaceArray<node> F(E);

	OGDF_ASSERT(PG.representsCombEmbedding());
	OGDF_ASSERT(F.valid());


	// NETWORK VARIABLES

	Graph Network; //the dual network
	EdgeArray<int> lowerBound(Network, 0); // lower bound for flow
	EdgeArray<int> upperBound(Network, 0); // upper bound for flow

	EdgeArray<int> cost(Network, 0); // cost of an edge
	NodeArray<int> supply(Network, 0); // supply of every node


	//alignment helper
	NodeArray<bool> fixedVal(Network, false); //already set somewhere
	EdgeArray<bool> noBendEdge(Network, false); //for splitter, brother edges etc.

	//NETWORK TO PlanRep INFORMATION

	// stores for edges of the Network the corresponding adjEntries
	// nodes, and faces of PG
	EdgeArray<adjEntry> adjCor(Network, nullptr);
	EdgeArray<node> nodeCor(Network, nullptr);
	EdgeArray<face> faceCor(Network, nullptr);

	NodeArray<NetworkNodeType> nodeTypeArray(Network, NetworkNodeType::low);

	//PlanRep TO NETWORK INFORMATION

	//Contains for every node of PG the corresponding node in the network
	NodeArray<node> networkNode(PG, nullptr);
	//Contains for every adjEntry of PG the corresponding edge in the network
	AdjEntryArray<edge> backAdjCor(PG, nullptr); //bends
	//contains for every adjEntry of PG the corresponding angle arc in the network
	//note: this doesn't need to correspond to resulting drawing angles
	//bends on the boundary define angles at expanded nodes
	AdjEntryArray<edge> angleArc(PG, nullptr); //angle
	//contains the corresponding back arc face to node in progressive mode
	AdjEntryArray<edge> angleBackArc(PG, nullptr); //angle

	// OTHER INFORMATION

	// Contains for adjacency Entry of PG the face it belongs to in PG
	AdjEntryArray<face> adjF(PG, nullptr);

	//Contains for angle network arc progressive mode backward arc
	EdgeArray<edge> angleTwin(Network, nullptr);

	auto setProgressiveBoundsEqually = [&](edge e, int flow, int flowTwin) {
		upperBound[e] = lowerBound[e] = flow;
		const edge aTwin = angleTwin[e];
		if (aTwin) {
			upperBound[aTwin] = lowerBound[aTwin] = flowTwin;
		}
	};
	auto setBoundsEqually = [&](edge e, int flowTraditional, int flowProgressive,
									int flowProgressiveTwin) {
		if (m_traditional) {
			upperBound[e] = lowerBound[e] = flowTraditional;
		} else {
			setProgressiveBoundsEqually(e, flowProgressive, flowProgressiveTwin);
		}
	};

	//types of network edges, to be used in flow to values
	EdgeArray<NetArcType> l_arcType(Network, NetArcType::angle);

	//contains the outer face
	//face theOuterFace = E.externalFace();

	// GENERATE ALL NODES OF THE NETWORK

	//corresponding to the graphs nodes
	for (node v : PG.nodes) {
		OGDF_ASSERT((!m_fourPlanar) || (v->degree() < 5));

		networkNode[v] = Network.newNode();
		//maybe install a shortcut here for degree 4 nodes if not expanded

		if (v->degree() > 4) {
			nodeTypeArray[networkNode[v]] = NetworkNodeType::high;
		} else {
			nodeTypeArray[networkNode[v]] = NetworkNodeType::low;
		}

		//already set the supply
		if (m_traditional) {
			supply[networkNode[v]] = 4;
		} else {
			supply[networkNode[v]] = 2 * v->degree() - 4;
		}
	}

	//corresponding to the graphs faces
	for (face f : E.faces) {
		F[f] = Network.newNode();

		if (f == E.externalFace()) {
			nodeTypeArray[F[f]] = NetworkNodeType::outer;
			if (m_traditional) {
				supply[F[f]] = -2 * f->size() - 4;
			} else {
				supply[F[f]] = 4;
			}
		} else {
			nodeTypeArray[F[f]] = NetworkNodeType::inner;
			if (m_traditional) {
				supply[F[f]] = -2 * f->size() + 4;
			} else {
				supply[F[f]] = -4;
			}
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	//check the supply sum
	int checksum = 0;
	for (node v : Network.nodes) {
		checksum += supply[v];
	}
	OGDF_ASSERT(checksum == 0);

	for (node v : PG.nodes) {
		Logger::slout() << " v = " << v << " corresponds to " << networkNode[v] << std::endl;
	}
	for (face f : E.faces) {
		Logger::slout() << " face = " << f->index() << " corresponds to " << F[f];
		if (f == E.externalFace()) {
			Logger::slout() << " (Outer Face)";
		}
		Logger::slout() << std::endl;
	}
#endif


	// GENERATE ALL EDGES OF THE NETWORK

	// OPTIMIZATION POTENTIAL:
	// Do not insert edges with upper bound 0 into the network.

	// Locate for every adjacency entry its adjacent faces.
	for (face f : E.faces) {
		for (adjEntry adj : f->entries) {
			adjF[adj] = f;
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	for (face f : E.faces) {
		Logger::slout() << "Face " << f->index() << " : ";
		for (adjEntry adj : f->entries) {
			Logger::slout() << adj << "; ";
		}
		Logger::slout() << std::endl;
	}
#endif


	// Insert for every edge the (two) network arcs
	// entering the face nodes, flow defines bends on the edge
	for (edge e : PG.edges) {
		OGDF_ASSERT(adjF[e->adjSource()]);
		OGDF_ASSERT(adjF[e->adjTarget()]);
		if (F[adjF[e->adjSource()]] != F[adjF[e->adjTarget()]]) {
			// not a selfloop.
			edge newE = Network.newEdge(F[adjF[e->adjSource()]], F[adjF[e->adjTarget()]]);

			l_arcType[newE] = NetArcType::bend;

			adjCor[newE] = e->adjSource();
			if ((PG.typeOf(e) == Graph::EdgeType::generalization)
					|| (PG.isBoundary(e) && (!m_traditional))) {
				upperBound[newE] = 0;
			} else {
				upperBound[newE] = infinity;
			}
			cost[newE] = (m_traditional ? tradBendCost : progBendCost);
			//cost[newE] = 1;
			backAdjCor[e->adjSource()] = newE;

			newE = Network.newEdge(F[adjF[e->adjTarget()]], F[adjF[e->adjSource()]]);

			l_arcType[newE] = NetArcType::bend;

			adjCor[newE] = e->adjTarget();
			if ((PG.typeOf(e) == Graph::EdgeType::generalization)
					|| (PG.isBoundary(e) && (m_traditional))) {
				upperBound[newE] = 0;
			} else {
				upperBound[newE] = infinity;
			}
			cost[newE] = (m_traditional ? tradBendCost : progBendCost);
			//cost[newE] = 1;
			backAdjCor[e->adjTarget()] = newE;
		}
	}


	// insert for every node edges to all appearances of adjacent faces
	// flow defines angles at nodes
	// progressive: and vice-versa


	// Observe that two generalizations are not allowed to bend on
	// a node. There must be a 180 degree angle between them.

	// assure that there is enough flow between adjacent generalizations
	NodeArray<bool> genshift(PG, false);

	//non-expanded vertex
	for (node v : PG.nodes) {
		// Locate possible adjacent generalizations
		adjEntry gen1 = nullptr;
		adjEntry gen2 = nullptr;

		if (PG.typeOf(v) != Graph::NodeType::generalizationMerger
				&& PG.typeOf(v) != Graph::NodeType::generalizationExpander) {
			for (adjEntry adj : v->adjEntries) {
				if (PG.typeOf(adj->theEdge()) == Graph::EdgeType::generalization) {
					if (!gen1) {
						gen1 = adj;
					} else {
						gen2 = adj;
					}
				}
			}
		}

		for (adjEntry adj : v->adjEntries) {
			edge e2 = Network.newEdge(networkNode[v], F[adjF[adj]]);

			l_arcType[e2] = NetArcType::angle;
			//CHECK bounded edges? and upper == 2 for zero degree
			//progressive and traditional
			upperBound[e2] = upperAngleFlow;
			nodeCor[e2] = v;
			adjCor[e2] = adj;
			faceCor[e2] = adjF[adj];
			angleArc[adj] = e2;

			//do not allow zero degree at non-expanded vertex
			//&& !m_allowLowZero
			//progressive and traditional (compatible)
			if (m_fourPlanar) {
				lowerBound[e2] = lowerAngleFlow; //trad 1 = 90, prog 0 = 180
			}

			//insert opposite arcs face to node in progressive style
			if (!m_traditional) {
				edge e3 = Network.newEdge(F[adjF[adj]], networkNode[v]); //flow for >180 degree

				l_arcType[e3] = NetArcType::backAngle;

				angleTwin[e2] = e3;
				angleTwin[e3] = e2;

				cost[e2] = progAngleCost;
				cost[e3] = progAngleCost;

				lowerBound[e3] = lowerAngleFlow; //180 degree,check highdegree drawings
				upperBound[e3] = upperBackAngleFlow; //infinity;
				//nodeCor   [e3] = v; has no node, is face to node
				adjCor[e3] = adj;
				faceCor[e3] = adjF[adj];
				angleBackArc[adj] = e3;
			}
		}

		//second run to have all angleArcs already initialized
		//set the flow boundaries for special cases
		//association classes
		adjEntry assClassAdj = nullptr;
		for (adjEntry adj : v->adjEntries) {
			//save the entry opposite to an association class
			//(only at the edgeToedge connection node)

			if ((v->degree() != 1) && (PG.isAssClass(adj->theEdge()))) {
				OGDF_ASSERT(assClassAdj == nullptr);
				assClassAdj = adj->cyclicSucc();
			}

			edge e2 = angleArc[adj];

			//hier muss man fuer die Kanten, die rechts ansetzen noch lowerbound 2 setzen

			// NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage): adj can never be nullptr
			if ((gen2 == adj && gen1 == adj->cyclicSucc())
					|| (gen1 == adj && gen2 == adj->cyclicSucc())) {
				setBoundsEqually(e2, piAngleFlow, piAngleFlow, 0);
				genshift[v] = true;
			}
		}
		//process special case of association classes: 180 degree angle
		if (assClassAdj != nullptr) {
			edge e2 = angleArc[assClassAdj];
			lowerBound[e2] = piAngleFlow;

			if (!m_traditional) {
				edge e3 = angleTwin[e2];
				upperBound[e3] = lowerBound[e3] = 0;
			}
		}
	}

	// Reset upper and lower Bounds for network arcs that
	// correspond to edges of generalizationmerger faces
	// and edges of expanded nodes.

	for (node v : PG.nodes) {
		if (PG.expandAdj(v)) {
			// Get the corresponding face in the original embedding.
			face f = adjF[PG.expandAdj(v)];

			//expanded merger cages
			if (PG.typeOf(v) == Graph::NodeType::generalizationMerger) {
				// Set upperBound to 0  for all edges.
				for (adjEntry adj : f->entries) {
					//no bends on boundary (except special case following)
					upperBound[backAdjCor[adj]] = 0;
					upperBound[backAdjCor[adj->twin()]] = 0;

					// Node w is in Network
					node w = networkNode[adj->twinNode()];
					for (adjEntry adjW : w->adjEntries) {
						edge e = adjW->theEdge();
						if (e->target() == F[f]) {
							// is this: 180 degree?
							// traditional: 2 progressive: 0
							// if not traditional limit angle back arc
							setBoundsEqually(e, piAngleFlow, piAngleFlow, 0);
						}
					}
				}
				//special bend case
				// Set the upper and lower bound for the first edge of
				// the mergeexpander face to guarantee a 90 degree bend.
				if (m_traditional) {
					upperBound[backAdjCor[PG.expandAdj(v)]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)]] = 1;
				} else {
					//progressive mode: bends are in opposite direction
					upperBound[backAdjCor[PG.expandAdj(v)->twin()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->twin()]] = 1;
				}

				// Set the upper and lower bound for the first node in
				// clockwise order of the mergeexpander face to
				// guaranty a 90 degree angle at the node in the interior
				// and a 180 degree angle between the generalizations in the
				// exterior.
				node secFace;

				if (F[f] == backAdjCor[PG.expandAdj(v)]->target()) {
					secFace = backAdjCor[PG.expandAdj(v)]->source();
				} else {
					OGDF_ASSERT(F[f] == backAdjCor[PG.expandAdj(v)]->source());
					secFace = backAdjCor[PG.expandAdj(v)]->target();
				}

				node w = networkNode[PG.expandAdj(v)->twinNode()];

				adjEntry adjFound = nullptr;
				for (adjEntry adj : w->adjEntries) {
					if (adj->theEdge()->target() == F[f]) {
						// if not traditional limit angle back arc
						setBoundsEqually(adj->theEdge(), 1, 1, 0);
						adjFound = adj;
						break;
					}
				}
				OGDF_ASSERT(adjFound);

				edge e;
				if (m_traditional) {
					e = adjFound->cyclicSucc()->theEdge();
				} else {
					//we have two edges instead of one per face
					adjEntry ae = adjFound->cyclicSucc();
					e = ae->theEdge();
					if (e->target() != secFace) {
						//maybe we have to jump one step further
						e = ae->cyclicSucc()->theEdge();
					}
				}

				if (e->target() == secFace) {
					setBoundsEqually(e, piAngleFlow, piAngleFlow, piAngleFlow);
				}

				// Set the upper and lower bound for the last edge of
				// the mergeexpander face to guarantee a 90 degree bend.
				if (m_traditional) {
					upperBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()]] = 1;
				} else {
					//progressive mode: bends are in opposite direction
					upperBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()->twin()]] = 1;
					lowerBound[backAdjCor[PG.expandAdj(v)->faceCyclePred()->twin()]] = 1;
				}

				// Set the upper and lower bound for the last node in
				// clockwise order of the mergeexpander face to
				// guaranty a 90 degree angle at the node in the interior
				// and a 180 degree angle between the generalizations in the
				// exterior.
				if (F[f] == backAdjCor[PG.expandAdj(v)->faceCyclePred()]->target()) {
					secFace = backAdjCor[PG.expandAdj(v)->faceCyclePred()]->source();
				} else {
					OGDF_ASSERT(F[f] == backAdjCor[PG.expandAdj(v)->faceCyclePred()]->source());
					secFace = backAdjCor[PG.expandAdj(v)->faceCyclePred()]->target();
				}

				w = networkNode[PG.expandAdj(v)->faceCyclePred()->theNode()];

				adjFound = nullptr;
				for (adjEntry adj : w->adjEntries) {
					if (adj->theEdge()->target() == F[f]) {
						// if not traditional limit angle back arc
						setBoundsEqually(adj->theEdge(), 1, 1, 0);
						adjFound = adj;
						break;
					}
				}
				OGDF_ASSERT(adjFound);

				if (m_traditional) {
					e = adjFound->cyclicPred()->theEdge();
				} else {
					//we have two edges instead of one per face
					adjEntry ae = adjFound->cyclicPred();
					e = ae->theEdge();
					if (e->target() != secFace) {
						//maybe we have to jump one step further
						e = ae->cyclicPred()->theEdge();
					}
				}

				if (e->target() == secFace) {
					setBoundsEqually(e, piAngleFlow, piAngleFlow, piAngleFlow);
				}
			}

			//expanded high degree cages
			else if (PG.typeOf(v) == Graph::NodeType::highDegreeExpander) {
				// Set upperBound to 1 for all edges, allowing maximal one
				// 90 degree bend.
				// Set upperBound to 0 for the corresponding entering edge
				// allowing no 270 degree bend.
				// Set upperbound to 1 for every edge corresponding to the
				// angle of a vertex. This permitts 270 degree angles in
				// the face

				adjEntry splitter = nullptr;


				//assure that edges are only spread around the sides if not too
				//many multi edges are aligned

				//count multiedges at node
				int multis = 0;
				AdjEntryArray<bool> isMulti(PG, false);
				if (m_multiAlign) {
					//if all edges are multi edges, find a 360 degree position
					bool allMulti = true;
					for (adjEntry adj : f->entries) //this double iteration slows the algorithm down
					{
						adjEntry srcadj = adj->cyclicPred();
						adjEntry tgtadj = adj->twin()->cyclicSucc();
						//check if the nodes are expanded
						node vt1, vt2;
						if (PG.expandedNode(srcadj->twinNode())) {
							vt1 = PG.expandedNode(srcadj->twinNode());
						} else {
							vt1 = srcadj->twinNode();
						}
						if (PG.expandedNode(tgtadj->twinNode())) {
							vt2 = PG.expandedNode(tgtadj->twinNode());
						} else {
							vt2 = tgtadj->twinNode();
						}
						if (vt1 == vt2) {
							//we forbid bends between two incident multiedges
							if (m_traditional) {
								lowerBound[backAdjCor[adj]] = upperBound[backAdjCor[adj]] = 0;
								isMulti[adj] = true;
							} else {
								lowerBound[backAdjCor[adj->twin()]] = lowerBound[backAdjCor[adj]] =
										upperBound[backAdjCor[adj]] =
												upperBound[backAdjCor[adj->twin()]] = 0;
								isMulti[adj->twin()] = true;
							}
							multis++;
						} else {
							allMulti = false;
						}
					}
					//multi edge correction: only multi edges => one edge needs 360 degree
					if (allMulti) {
						//find an edge that allows 360 degree without bends
						bool twoNodeCC = true; //no foreign non-multi edge to check for
						for (adjEntry adj : f->entries) {
							//now check for expanded nodes
							adjEntry adjOut = adj->cyclicPred(); //outgoing edge entry
							node vOpp = adjOut->twinNode();
							if (PG.expandedNode(vOpp)) {
								adjOut = adjOut->faceCycleSucc(); //on expanded boundary
								//does not end on self loops
								node vStop = vOpp;
								if (PG.expandedNode(vStop)) {
									vStop = PG.expandedNode(vStop);
								}
								while (PG.expandedNode(adjOut->twinNode()) == vStop) {
									//we are still on vOpps cage
									adjOut = adjOut->faceCycleSucc();
								}
							}
							//now adjOut is either a "foreign" edge or one of the
							//original multi edges if two-node-CC
							//adjEntry testadj = adjCor[e]->faceCycleSucc()->twin();
							adjEntry testAdj = adjOut->twin();
							node vBack = testAdj->theNode();
							if (PG.expandedNode(vBack)) {
								vBack = PG.expandedNode(vBack);
							}
							if (vBack != v) //v is expanded node
							{
								//dont use iteration result, set firstedge!
								upperBound[backAdjCor[adj]] = 4; //4 bends for 360
								twoNodeCC = false;
								break;
							}
						}
						//if only two nodes with multiedges are in current CC,
						//assign 360 degree to first edge
						if (twoNodeCC) {
							//it would be difficult to guarantee that the networkedge
							//on the other side of the face would get the 360, so alllow
							//360 for all edges or search for the outer face

							for (adjEntry adj : f->entries) {
								adjEntry ae = adj->cyclicPred();
								if (adjF[ae] == E.externalFace()) {
									// 4 bends for 360
									upperBound[backAdjCor[adj]] = 4;
									break;
								}
							}
						}
					}
					//End multi edge correction
				}

				//now set the upper Bounds
				for (adjEntry adj : f->entries) {
					//should be: no 270 degrees
					if (m_traditional) {
						upperBound[backAdjCor[adj->twin()]] = 0;
					} else {
						upperBound[backAdjCor[adj]] = 0;
					}

					//should be: only one bend
					{
						if (m_distributeEdges)
						//CHECK
						//maybe we should change this to setting the lower bound too,
						//depending on the degree (<= 4 => deg 90)
						{
							//check the special case degree >=4 with 2
							// generalizations following each other if degree
							// > 4, only 90 degree allowed, nodeType high
							// bloed, da nicht original
							//if (nodeTypeArray[ networkNode[adj->twinNode()] ] == high)
							//hopefully size is original degree
							if (m_traditional) {
								if (!isMulti[adj]) //m_multiAlign???
								{
									//check if original node degree minus multi edges
									//is high enough
									//Attention: There are some lowerBounds > 1
#ifdef OGDF_DEBUG
									int oldBound =
#endif
											upperBound[backAdjCor[adj]];
									if ((!genshift[v]) && (f->size() - multis > 3)) {
										upperBound[backAdjCor[adj]] =
												max(1, lowerBound[backAdjCor[adj]]);
										// max(2, lowerBound[backAdjCor[adj]]);
										// due to mincostflowreinelt errors, we are not
										// allowed to set ub 1
									} else {
										upperBound[backAdjCor[adj]] =
												max(2, lowerBound[backAdjCor[adj]]);
									}
									//nur zum Testen der Faelle
									OGDF_ASSERT(oldBound >= upperBound[backAdjCor[adj]]);
								}
							} else {
								//preliminary set the bound in all cases

								if (!isMulti[adj]) //m_multiAlign???
								{
									//Attention: There are some lowerBounds > 1
									if ((!genshift[v]) && (f->size() - multis > 3)) {
										upperBound[backAdjCor[adj->twin()]] =
#if 1
												max(1, lowerBound[backAdjCor[adj->twin()]]);
									}
#else
												max(1, lowerBound[backAdjCor[adj]]);
#endif
									else {
#if 1
										upperBound[backAdjCor[adj->twin()]] =
												max(2, lowerBound[backAdjCor[adj->twin()]]);
#else
											upperBound[backAdjCor[adj]] =
													max(2, lowerBound[backAdjCor[adj]]);
#endif
									}
								}
							}
						}
					}

					// Node w is in Network
					node w = networkNode[adj->twinNode()];

					// if (w && !(m_traditional && m_fourPlanar && (w->degree() != 4)))
					{
						//should be: inner face angles set to 180
						for (adjEntry adjW : w->adjEntries) {
							edge e = adjW->theEdge();
							if (e->target() == F[f]) {
								setBoundsEqually(e, piAngleFlow, piAngleFlow, piAngleFlow);
							}
						}
					}
				}

				// In case a face splitter was used, we need to update the
				// second face of the cage.
				if (splitter) {
					// Get the corresponding face in the original embedding.
					face f2 = adjF[splitter->twin()];

					for (adjEntry adj : f2->entries) {
						if (adj == splitter->twin()) {
							continue;
						}

						//todo: Alignment

						if (m_traditional) {
							upperBound[backAdjCor[adj->twin()]] = 0;
						} else { //progressive bends are in opposite direction
							upperBound[backAdjCor[adj]] = 0;
						}

						// Node w is in Network
						node w = networkNode[adj->twinNode()];
						// if (w && !(m_traditional && m_fourPlanar && (w->degree() != 4)))
						{
							for (adjEntry adjW : w->adjEntries) {
								edge e = adjW->theEdge();
								if (e->target() == F[f2]) {
									setBoundsEqually(e, piAngleFlow, piAngleFlow, piAngleFlow);
								}
							}
						}
					}
				}
			}
		}

		else {
			//non-expanded (low degree) nodes
			//check for alignment and for multi edges

			//check for multi edges and decrease lowerbound if align
#if 0
			int lowerb = 0;
#endif

			if (PG.isVertex(v)) {
				node w = networkNode[v];
				if ((nodeTypeArray[w] != NetworkNodeType::low) || (w->degree() < 2)) {
					continue;
				}

				bool allMulti = true;
				for (adjEntry adj : w->adjEntries) {
					edge e = adj->theEdge();
#if 0
					lowerb += max(lowerBound[e], 0);
#endif

					OGDF_ASSERT((!m_traditional) || (e->source() == w));
					if (m_traditional && (e->source() != w)) {
						OGDF_THROW(AlgorithmFailureException);
					}
					if (e->source() != w) {
						continue; //dont treat back angle edges
					}

					if (m_multiAlign && (v->degree() > 1)) {
						adjEntry srcAdj = adjCor[e];
						adjEntry tgtAdj = adjCor[e]->faceCyclePred();

						//check if the nodes are expanded
						node vt1, vt2;
						if (PG.expandedNode(srcAdj->twinNode())) {
							vt1 = PG.expandedNode(srcAdj->twinNode());
						} else {
							vt1 = srcAdj->twinNode();
						}

						if (PG.expandedNode(tgtAdj->theNode())) {
							vt2 = PG.expandedNode(tgtAdj->theNode());
						} else {
							vt2 = tgtAdj->theNode();
						}

						if (vt1 == vt2) {
							fixedVal[w] = true;

							//we forbid bends between incident multi edges
							//or is it angle?
							setBoundsEqually(e, zeroAngleFlow, zeroAngleFlow, zeroBackAngleFlow);
						} else {
							//CHECK
							//to be done: only if multiedges
							if (!genshift[v]) {
								upperBound[e] = upperAngleFlow;
							}
							allMulti = false;
						}
					}
				}

				if (m_multiAlign && allMulti && (v->degree() > 1)) {
					fixedVal[w] = true;

					//find an edge that allows 360 degree without bends
					bool twoNodeCC = true;
					for (adjEntry adj : w->adjEntries) {
						edge e = adj->theEdge();
#if 0
						if (PG.expandedNode(srcAdj->twinNode())) {
							vt1 = PG.expandedNode(srcAdj->twinNode());
						}
#endif
						//now check for expanded nodes
						adjEntry runAdj = adjCor[e];
						node vOpp = runAdj->twinNode();
						node vStop;
						vStop = vOpp;
						runAdj = runAdj->faceCycleSucc();
						if (PG.expandedNode(vStop)) {
							//does not end on self loops
							vStop = PG.expandedNode(vStop);
							while (PG.expandedNode(runAdj->twinNode()) == vStop) {
								//we are still on vOpps cage
								runAdj = runAdj->faceCycleSucc();
							}
						}
						//adjEntry testadj = adjCor[e]->faceCycleSucc()->twin();
						adjEntry testAdj = runAdj->twin();
						node vBack = testAdj->theNode();

						if (vBack != v) //not same node
						{
							if (PG.expandedNode(vBack)) {
								vBack = PG.expandedNode(vBack);
							}
							if (vBack != vStop) //vstop !=0, not inner face in 2nodeCC
							{
								//CHECK: 4? upper
								OGDF_ASSERT(!PG.expandedNode(v)); //otherwise not angle flow
								if (m_traditional) {
									// dont use iteration result, set firstedge!
									upperBound[e] = maxAngleFlow;
								} else {
									setProgressiveBoundsEqually(e, lowerAngleFlow, maxBackFlow);
								}
								twoNodeCC = false;
								break;
							}
						}
					}
					//if only two nodes with multiedges are in current CC,
					//assign 360 degree to first edge
					if (twoNodeCC) {
						//it would be difficult to guarantee that the networkedge
						//on the other side of the face would get the 360, so allow
						//360 for all edges or search for external face
						for (adjEntry adj : w->adjEntries) {
							edge e = adj->theEdge();
							adjEntry adje = adjCor[e];
							if (adjF[adje] == E.externalFace()) {
								//CHECK: 4? upper
								OGDF_ASSERT(!PG.expandedNode(v)); //otherwise not angle flow
								if (m_traditional) {
									upperBound[e] = maxAngleFlow; //upperAngleFlow;
								} else {
									setProgressiveBoundsEqually(e, lowerAngleFlow, maxBackFlow);
								}
								break;
							}
						}
					}
				}
			}
		}
	}

	//int flowSum = 0;

	//To Be done: hier multiedges testen
	for (node tv : Network.nodes) {
		//flowSum += supply[tv];

		//only check representants of original nodes, not faces
		if (nodeTypeArray[tv] == NetworkNodeType::low || nodeTypeArray[tv] == NetworkNodeType::high) {
			//if node representant with degree 4, set angles preliminary
			//degree four nodes with two gens are expanded in PlanRep
			//all others are allowed to change the edge positions
			if ((m_traditional && (tv->degree() == 4)) || ((tv->degree() == 8) && !m_traditional)) {
				//three types: degree4 original nodes and facesplitter end nodes,
				//maybe crossings
				//fixassignment tells us that low degree nodes are not allowed to
				//have zero degree and special nodes are already assigned
				bool fixAssignment = true;

				//check if free assignment is possible for degree 4
				if (m_deg4free) {
					fixAssignment = false;
					for (adjEntry adj : tv->adjEntries) {
						edge te = adj->theEdge();
						if (te->source() == tv) {
							adjEntry pgEntry = adjCor[te];
							node pgNode = pgEntry->theNode();

							if ((PG.expandedNode(pgNode))
									|| (PG.typeOf(pgNode) == Graph::NodeType::dummy) //test crossings
							) {
								fixAssignment = true;
								break;
							}
						}
					}
				}

				//CHECK
				//now set the angles at degree 4 nodes to distribute edges
				for (adjEntry adj : tv->adjEntries) {
					edge te = adj->theEdge();

					if (te->source() == tv) {
						if (fixedVal[tv]) {
							continue; //if already special values set
						}

						if (!fixAssignment) {
							lowerBound[te] = 0; //lowerAngleFlow maybe 1;//0;
							upperBound[te] = upperAngleFlow; //4;
						} else {
							//only allow 90 degree arc value
							lowerBound[te] = halfPiAngleFlow;
							upperBound[te] = halfPiAngleFlow;
						}
					} else {
						if (fixedVal[tv]) {
							continue; //if already special values set
						}

						if (!fixAssignment) {
							OGDF_ASSERT(lowerAngleFlow == 0); //should only be in progressive mode
							lowerBound[te] = lowerAngleFlow;
							upperBound[te] = upperBackAngleFlow;
						} else {
							//only allow 0-180 degree back arc value
							lowerBound[te] = 0; //1
							upperBound[te] = 0; //halfPiAngleFlow; //1
						}
					}
				}
			}
#ifdef OGDF_DEBUG
			int lowsum = 0, upsum = 0;
#endif
			for (adjEntry adj : tv->adjEntries) {
				edge te = adj->theEdge();
				OGDF_ASSERT(lowerBound[te] <= upperBound[te]);
				if (noBendEdge[te]) {
					lowerBound[te] = 0;
				}
#ifdef OGDF_DEBUG
				lowsum += lowerBound[te];
				upsum += upperBound[te];
#endif
			}
			if (m_traditional) {
				OGDF_ASSERT(lowsum <= supply[tv]);
				OGDF_ASSERT(upsum >= supply[tv]);
			}
		}
	}

	for (node tv : Network.nodes) {
		for (adjEntry adj : tv->adjEntries) {
			edge te = adj->theEdge();
			if (noBendEdge[te]) {
				lowerBound[te] = 0;
			}
		}
	}

	bool isFlow = false;
	SList<edge> capacityBoundedEdges;
	EdgeArray<int> flow(Network, 0);

	// Set upper Bound. Do not leave it to INT_NAX.
	// Causes problems with MinCostFlowReinelt

	//but some edges are no longer capacitybounded, therefore save their status
	EdgeArray<bool> isBounded(Network, false);

	for (edge e : Network.edges) {
		if (upperBound[e] == infinity) {
			capacityBoundedEdges.pushBack(e);
			isBounded[e] = true;
		}
	}

	int currentUpperBound;
	if (m_startBoundBendsPerEdge > 0) {
		currentUpperBound = m_startBoundBendsPerEdge;
	} else {
		currentUpperBound = 4 * PG.numberOfEdges();
	}

	while ((!isFlow) && (currentUpperBound <= 4 * PG.numberOfEdges())) {
		for (edge ei : capacityBoundedEdges) {
			upperBound[ei] = currentUpperBound;
		}

		isFlow = flowModule.call(Network, lowerBound, upperBound, cost, supply, flow);

#if 0
		if (isFlow)
		{ //if (int(ogdf::debugLevel) >= int(dlHeavyChecks)) {
			for(edge e : Network.edges) {
				fout << "e = " << e << " flow = " << flow[e];
				if(nodeCor[e] == 0 && adjCor[e])
					fout << " real edge = " << adjCor[e]->theEdge();
				fout << std::endl;
			}
			for(edge e : Network.edges) {
				if(nodeCor[e] == 0 && adjCor[e] != 0 && flow[e] > 0) {
					fout << "Bends " << flow[e] << " on edge "
						<< adjCor[e]->theEdge()
						<< " between faces " << adjF[adjCor[e]]->index() << " - "
						<< adjF[adjCor[e]->twin()]->index() << std::endl;
				}
			}
			for(edge e : Network.edges) {
				if(nodeCor[e] != 0 && faceCor[e] != 0) {
					fout << "Angle " << (flow[e])*90 << "\tdegree   on node "
						<< nodeCor[e] << " at face " << faceCor[e]->index()
						<< "\tbetween edge " << adjCor[e]->faceCyclePred()
						<< "\tand " << adjCor[e] << std::endl;
				}
			}
			if (startBoundBendsPerEdge> 0) {
				fout << "Minimizing edge bends for upper bound "
					<< currentUpperBound;
				if(isFlow)
					fout << " ... Successful";
				fout << std::endl;
			}
		}
#endif

		OGDF_ASSERT(m_startBoundBendsPerEdge >= 1 || isFlow);

		currentUpperBound++;
	}

	if (m_startBoundBendsPerEdge && !isFlow) {
		OGDF_THROW_PARAM(AlgorithmFailureException, AlgorithmFailureCode::NoFlow);
	}

#ifdef OGDF_HEAVY_DEBUG
	int totalNumBends = 0;
#endif

	//int gap = currentUpperBound;

	for (edge e : Network.edges) {
		if (nodeCor[e] == nullptr && adjCor[e] != nullptr && (flow[e] > 0)
				&& (angleTwin[e] == nullptr)) //no angle edges
		{
			OGDF_ASSERT(OR.bend(adjCor[e]).size() == 0);

			char zeroChar = (m_traditional ? '0' : '1');
			char oneChar = (m_traditional ? '1' : '0');
			//we depend on the property that there is no flow
			//in opposite direction due to the cost
			OR.bend(adjCor[e]).set(zeroChar, flow[e]);
			OR.bend(adjCor[e]->twin()).set(oneChar, flow[e]);

#ifdef OGDF_HEAVY_DEBUG
			totalNumBends += flow[e];
#endif

			//check if bends fit bounds
#if 0
			if (isBounded[e])
			{
				OGDF_ASSERT((int)OR.bend(adjCor[e]).size() <= currentUpperBound);
				OGDF_ASSERT((int)OR.bend(adjCor[e]->twin()).size() <= currentUpperBound);
			}
#endif
		} else if (nodeCor[e] != nullptr && faceCor[e] != nullptr) {
			if (m_traditional) {
				OR.angle(adjCor[e]) = (flow[e]);
			} else {
				OGDF_ASSERT(angleTwin[e]);
				OGDF_ASSERT(flow[e] >= 0);
				OGDF_ASSERT(flow[e] <= 2);

				const int twinFlow = flow[angleTwin[e]];

				if (flow[e] == 0) {
					OGDF_ASSERT(twinFlow >= 0);
					OGDF_ASSERT(twinFlow <= 2);

					OR.angle(adjCor[e]) = 2 + twinFlow;
				} else {
					OGDF_ASSERT(twinFlow == 0);
					OR.angle(adjCor[e]) = 2 - flow[e];
				}
			}
		}
	}

#ifdef OGDF_HEAVY_DEBUG
	Logger::slout() << "\n\nTotal Number of Bends : " << totalNumBends << std::endl << std::endl;

	string error;
	if (!OR.check(error)) {
		Logger::slout() << error << std::endl;
		OGDF_ASSERT(false);
	}
#endif
}

}
