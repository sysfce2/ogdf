/** \file
 * \brief Declaration of visibility layout algorithm.
 *
 * \author Hoi-Ming Wong
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

//***
// Visibility Layout Method. see "Graph Drawing" by Di Battista et al.
//***

#pragma once

#include <ogdf/basic/CombinatorialEmbedding.h>
#include <ogdf/basic/Graph.h>
#include <ogdf/basic/LayoutModule.h>
#include <ogdf/basic/basic.h>
#include <ogdf/upward/SubgraphUpwardPlanarizer.h>
#include <ogdf/upward/UpwardPlanarizerModule.h>

#include <memory>

namespace ogdf {
class GraphAttributes;
class UpwardPlanRep;

class OGDF_EXPORT VisibilityLayout : public LayoutModule {
public:
	VisibilityLayout() {
		m_grid_dist = 1;
		// set default module
		m_upPlanarizer.reset(new SubgraphUpwardPlanarizer());
	}

	virtual void call(GraphAttributes& GA) override;

	void layout(GraphAttributes& GA, const UpwardPlanRep& UPROrig);

	void setUpwardPlanarizer(UpwardPlanarizerModule* upPlanarizer) {
		m_upPlanarizer.reset(upPlanarizer);
	}

	void setMinGridDistance(int dist) { m_grid_dist = dist; }


private:
	//min grid distance
	int m_grid_dist;

	//node segment of the visibility representation
	struct NodeSegment {
		int y; //y coordinate
		int x_l; // left x coordinate
		int x_r; // right x coordiante
	};

	// edge segment of the visibility representation
	struct EdgeSegment {
		int y_b; // bottom y coordinate
		int y_t; // top y coordinate
		int x; // x coordiante
	};

	//mapping node to node segment of visibility presentation
	NodeArray<NodeSegment> nodeToVis;

	//mapping edge to edge segment of visibility presentation
	EdgeArray<EdgeSegment> edgeToVis;

	std::unique_ptr<UpwardPlanarizerModule> m_upPlanarizer; // upward planarizer

	void constructDualGraph(const UpwardPlanRep& UPR, Graph& D, node& s_D, node& t_D,
			FaceArray<node>& faceToNode, NodeArray<face>& leftFace_node,
			NodeArray<face>& rightFace_node, EdgeArray<face>& leftFace_edge,
			EdgeArray<face>& rightFace_edge);

	void constructVisibilityRepresentation(const UpwardPlanRep& UPR);
};

}
