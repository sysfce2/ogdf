/** \file
 * \brief Implementations of class GEMLayout.
 *
 * Fast force-directed layout algorithm (GEMLayout) based on Frick et al.'s algorithm
 *
 * \author Christoph Buchheim
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


#include <ogdf/basic/Array.h>
#include <ogdf/basic/EpsilonTest.h>
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/basic/GraphCopy.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/LayoutStandards.h>
#include <ogdf/basic/List.h>
#include <ogdf/basic/Math.h>
#include <ogdf/basic/SList.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/geometry.h>
#include <ogdf/basic/simple_graph_alg.h>
#include <ogdf/energybased/GEMLayout.h>
#include <ogdf/packing/TileToRowsCCPacker.h>

#include <cmath>
#include <random>

namespace ogdf {

GEMLayout::GEMLayout()
	: m_numberOfRounds(30000)
	, m_minimalTemperature(0.005)
	, m_initialTemperature(12.0)
	, m_gravitationalConstant(1.0 / 16.0)
	, //original paper value
	m_desiredLength(LayoutStandards::defaultNodeSeparation())
	, m_maximalDisturbance(0)
	, m_rotationAngle(Math::pi / 3.0)
	, m_oscillationAngle(Math::pi_2)
	, m_rotationSensitivity(0.01)
	, m_oscillationSensitivity(0.3)
	, m_attractionFormula(1)
	, m_minDistCC(LayoutStandards::defaultCCSeparation())
	, m_pageRatio(1.0)
	, m_rng(randomSeed()) { }

GEMLayout::GEMLayout(const GEMLayout& fl)
	: m_numberOfRounds(fl.m_numberOfRounds)
	, m_minimalTemperature(fl.m_minimalTemperature)
	, m_initialTemperature(fl.m_initialTemperature)
	, m_gravitationalConstant(fl.m_gravitationalConstant)
	, m_desiredLength(fl.m_desiredLength)
	, m_maximalDisturbance(fl.m_maximalDisturbance)
	, m_rotationAngle(fl.m_rotationAngle)
	, m_oscillationAngle(fl.m_oscillationAngle)
	, m_rotationSensitivity(fl.m_rotationSensitivity)
	, m_oscillationSensitivity(fl.m_oscillationSensitivity)
	, m_attractionFormula(fl.m_attractionFormula)
	, m_minDistCC(fl.m_minDistCC)
	, m_pageRatio(fl.m_pageRatio)
	, m_rng(randomSeed()) { }

GEMLayout::~GEMLayout() { }

GEMLayout& GEMLayout::operator=(const GEMLayout& fl) {
	m_numberOfRounds = fl.m_numberOfRounds;
	m_minimalTemperature = fl.m_minimalTemperature;
	m_initialTemperature = fl.m_initialTemperature;
	m_gravitationalConstant = fl.m_gravitationalConstant;
	m_desiredLength = fl.m_desiredLength;
	m_maximalDisturbance = fl.m_maximalDisturbance;
	m_rotationAngle = fl.m_rotationAngle;
	m_oscillationAngle = fl.m_oscillationAngle;
	m_rotationSensitivity = fl.m_rotationSensitivity;
	m_oscillationSensitivity = fl.m_oscillationSensitivity;
	m_attractionFormula = fl.m_attractionFormula;
	m_minDistCC = fl.m_minDistCC;
	m_pageRatio = fl.m_pageRatio;

	return *this;
}

void GEMLayout::call(GraphAttributes& AG) {
	const Graph& G = AG.constGraph();
	if (G.empty()) {
		return;
	}

	// all edges straight-line
	AG.clearAllBends();

	GraphCopy GC;
	GC.setOriginalGraph(G);

	// compute connected component of G
	NodeArray<int> component(G);
	int numCC = connectedComponents(G, component);

	// intialize the array of lists of nodes contained in a CC
	Array<List<node>> nodesInCC(numCC);

	for (node v : G.nodes) {
		nodesInCC[component[v]].pushBack(v);
	}

	NodeArray<node> nodeCopy;
	EdgeArray<edge> auxCopy;
	Array<DPoint> boundingBox(numCC);

	int i;
	for (i = 0; i < numCC; ++i) {
		nodeCopy.init(G);
		auxCopy.init(G);
		GC.clear();
		GC.insert(nodesInCC[i].begin(), nodesInCC[i].end(), filter_any_edge, nodeCopy, auxCopy);

		GraphAttributes AGC(GC);
		for (node vCopy : GC.nodes) {
			node vOrig = GC.original(vCopy);
			AGC.x(vCopy) = AG.x(vOrig);
			AGC.y(vCopy) = AG.y(vOrig);
		}

		SList<node> permutation;

		// initialize node data
		m_impulseX.init(GC, 0);
		m_impulseY.init(GC, 0);
		m_skewGauge.init(GC, 0);
		m_localTemperature.init(GC, m_initialTemperature);

		// initialize other data
		m_globalTemperature = m_initialTemperature;
		m_barycenterX = 0;
		m_barycenterY = 0;
		for (node v : GC.nodes) {
			m_barycenterX += weight(v) * AGC.x(v);
			m_barycenterY += weight(v) * AGC.y(v);
		}
		m_cos = cos(m_oscillationAngle / 2.0);
		m_sin = sin(Math::pi / 2 + m_rotationAngle / 2.0);

		// main loop
		int counter = m_numberOfRounds;
		while (OGDF_GEOM_ET.greater(m_globalTemperature, m_minimalTemperature) && counter-- > 0) {
			// choose nodes by random permutations
			if (permutation.empty()) {
				for (node v : GC.nodes) {
					permutation.pushBack(v);
				}
				permutation.permute(m_rng);
			}
			node v = permutation.popFrontRet();

			// compute the impulse of node v
			computeImpulse(GC, AGC, v);

			// update node v
			updateNode(GC, AGC, v);
		}

		node vFirst = GC.firstNode();
		double minX = AGC.x(vFirst), maxX = AGC.x(vFirst), minY = AGC.y(vFirst), maxY = AGC.y(vFirst);

		for (node vCopy : GC.nodes) {
			node v = GC.original(vCopy);
			AG.x(v) = AGC.x(vCopy);
			AG.y(v) = AGC.y(vCopy);

			Math::updateMin(minX, AG.x(v) - AG.width(v) / 2);
			Math::updateMax(maxX, AG.x(v) + AG.width(v) / 2);
			Math::updateMin(minY, AG.y(v) - AG.height(v) / 2);
			Math::updateMax(maxY, AG.y(v) + AG.height(v) / 2);
		}

		minX -= m_minDistCC;
		minY -= m_minDistCC;

		for (node vCopy : GC.nodes) {
			node v = GC.original(vCopy);
			AG.x(v) -= minX;
			AG.y(v) -= minY;
		}

		boundingBox[i] = DPoint(maxX - minX, maxY - minY);
	}

	Array<DPoint> offset(numCC);
	TileToRowsCCPacker packer;
	packer.call(boundingBox, offset, m_pageRatio);

	// The arrangement is given by offset to the origin of the coordinate
	// system. We still have to shift each node and edge by the offset
	// of its connected component.

	for (i = 0; i < numCC; ++i) {
		const double dx = offset[i].m_x;
		const double dy = offset[i].m_y;

		for (node v : nodesInCC[i]) {
			AG.x(v) += dx;
			AG.y(v) += dy;
		}
	}


	// free node data
	m_impulseX.init();
	m_impulseY.init();
	m_skewGauge.init();
	m_localTemperature.init();
}

void GEMLayout::computeImpulse(GraphCopy& G, GraphAttributes& AG, node v) {
	int n = G.numberOfNodes();

	double deltaX, deltaY, delta, deltaSqu;
	double desiredLength, desiredSqu;

	// add double node radius to desired edge length
	desiredLength = m_desiredLength + length(AG.height(v), AG.width(v));
	desiredSqu = desiredLength * desiredLength;

	// compute attraction to center of gravity
	m_newImpulseX = (m_barycenterX / n - AG.x(v)) * m_gravitationalConstant;
	m_newImpulseY = (m_barycenterY / n - AG.y(v)) * m_gravitationalConstant;

	// disturb randomly
	int maxIntDisturbance = (int)(m_maximalDisturbance * 10000);
	std::uniform_int_distribution<> dist(-maxIntDisturbance, maxIntDisturbance);
	m_newImpulseX += (dist(m_rng) / 10000.0);
	m_newImpulseY += (dist(m_rng) / 10000.0);

	// compute repulsive forces
	for (node u : G.nodes) {
		if (u != v) {
			deltaX = AG.x(v) - AG.x(u);
			deltaY = AG.y(v) - AG.y(u);
			delta = length(deltaX, deltaY);
			if (OGDF_GEOM_ET.greater(delta, 0.0)) {
				deltaSqu = delta * delta;
				m_newImpulseX += deltaX * desiredSqu / deltaSqu;
				m_newImpulseY += deltaY * desiredSqu / deltaSqu;
			}
		}
	}

	// compute attractive forces
	for (adjEntry adj : v->adjEntries) {
		node u = adj->twinNode();
		deltaX = AG.x(v) - AG.x(u);
		deltaY = AG.y(v) - AG.y(u);
		delta = length(deltaX, deltaY);
		if (m_attractionFormula == 1) {
			m_newImpulseX -= deltaX * delta / (desiredLength * weight(v));
			m_newImpulseY -= deltaY * delta / (desiredLength * weight(v));
		} else {
			deltaSqu = delta * delta;
			m_newImpulseX -= deltaX * deltaSqu / (desiredSqu * weight(v));
			m_newImpulseY -= deltaY * deltaSqu / (desiredSqu * weight(v));
		}
	}
}

void GEMLayout::updateNode(GraphCopy& G, GraphAttributes& AG, node v) {
	int n = G.numberOfNodes();
	double impulseLength;

	impulseLength = length(m_newImpulseX, m_newImpulseY);
	if (OGDF_GEOM_ET.greater(impulseLength, 0.0)) {
		// scale impulse by node temperature
		m_newImpulseX *= m_localTemperature[v] / impulseLength;
		m_newImpulseY *= m_localTemperature[v] / impulseLength;

		// move node
		AG.x(v) += m_newImpulseX;
		AG.y(v) += m_newImpulseY;

		// adjust barycenter
		m_barycenterX += weight(v) * m_newImpulseX;
		m_barycenterY += weight(v) * m_newImpulseY;

		impulseLength = length(m_newImpulseX, m_newImpulseY) * length(m_impulseX[v], m_impulseY[v]);
		if (OGDF_GEOM_ET.greater(impulseLength, 0.0)) {
			m_globalTemperature -= m_localTemperature[v] / n;

			// compute sine and cosine of angle between old and new impulse
			double sinBeta, cosBeta;
			sinBeta = (m_newImpulseX * m_impulseX[v] - m_newImpulseY * m_impulseY[v]) / impulseLength;
			cosBeta = (m_newImpulseX * m_impulseX[v] + m_newImpulseY * m_impulseY[v]) / impulseLength;

			// check for rotation
			if (OGDF_GEOM_ET.greater(sinBeta, m_sin)) {
				m_skewGauge[v] += m_rotationSensitivity;
			}

			// check for oscillation
			if (OGDF_GEOM_ET.greater(length(cosBeta), m_cos)) {
				m_localTemperature[v] *= (1 + cosBeta * m_oscillationSensitivity);
			}

			// cool down according to skew gauge
			m_localTemperature[v] *= (1.0 - length(m_skewGauge[v]));
			if (OGDF_GEOM_ET.geq(m_localTemperature[v], m_initialTemperature)) {
				m_localTemperature[v] = m_initialTemperature;
			}

			// adjust global temperature
			m_globalTemperature += m_localTemperature[v] / n;
		}

		// save impulse
		m_impulseX[v] = m_newImpulseX;
		m_impulseY[v] = m_newImpulseY;
	}
}

}
