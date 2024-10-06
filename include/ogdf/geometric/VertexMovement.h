/** \file
 * \brief In the VertexMovement Approach the vertices are moved one by one to its optimal position.
 * The optimal position can be for example a position that minimizes the number of crossings for this vertex.
 *
 * Based on the implementation and techniques of the following papers:
 * Marcel Radermacher, Klara Reichard, Ignaz Rutter, and Dorothea Wagner.
 * Geometric Heuristics for Rectilinear Crossing Minimization.
 * Journal of Experimental Algorithmics 24:1, pages 1.12:1–1.12:21, 2019. doi:10.1145/3325861 .
 *
 * Marcel Radermacher and Ignaz Rutter.
 * Geometric Crossing Minimization - A Scalable Randomized Approach.
 * In Proceedings of the 27th Annual European Symposium on Algorithms (ESA’19).
 * Ed. by Michael A. Bender, Ola Svensson, and Grzegorz Herman. Leibniz International Proceedings in Informatics (LIPIcs), pages 76:1–76:16.
 * Schloss Dagstuhl - Leibniz-Zentrum für Informatik, 2019. doi: 10.4230/LIPIcs.ESA
 *
 * \author Marcel Radermacher
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
#include <ogdf/basic/LayoutModule.h>
#include <ogdf/basic/memory.h>

namespace ogdf {
class GraphAttributes;
class VertexPositionModule;
template<class E>
class List;

class OGDF_EXPORT VertexMovement : public LayoutModule {
public:
	//! Constructor, sets options to default values.
	VertexMovement();
	~VertexMovement();

	//! The main call to the algorithm. GA should have nodeGraphics attributes enabled.
	virtual void call(GraphAttributes& GA) override;

	void setVertexPosition(VertexPositionModule* opt_pos) { m_pos = opt_pos; }

	void setVertexOrder(List<node>* vertex_order) { m_vertex_order = vertex_order; }


protected:
private:
	VertexPositionModule* m_pos = nullptr;
	List<node>* m_vertex_order = nullptr;

	OGDF_NEW_DELETE
};

}
