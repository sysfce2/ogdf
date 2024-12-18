/** \file
 * \brief Declaration of interface hierarchy layout algorithms
 *        (3. phase of Sugiyama).
 *
 * \author Carsten Gutwenger
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
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/basic/GraphCopy.h>
#include <ogdf/basic/GraphList.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/memory.h>
#include <ogdf/layered/CrossingMinInterfaces.h>
#include <ogdf/layered/Hierarchy.h>

namespace ogdf {


/**
 * \brief Interface of hierarchy layout algorithms.
 *
 * \see SugiyamaLayout
 */
class OGDF_EXPORT HierarchyLayoutModule {
public:
	//! Initializes a hierarchy layout module.
	HierarchyLayoutModule() { }

	virtual ~HierarchyLayoutModule() { }

	/**
	 * \brief Computes a hierarchy layout of \p levels in \p GA
	 * @param levels is the input hierarchy.
	 * @param GA is assigned the hierarchy layout.
	 */
	void call(const HierarchyLevelsBase& levels, GraphAttributes& GA) {
		GraphAttributes AGC(levels.hierarchy());

		// Copy over relevant nodeGraphics attributes that may be used by doCall
		// or need to be preserved. edgeGraphics' bend points need to be cleared
		// and are not copied over.
		if (GA.has(GraphAttributes::nodeGraphics)) {
			const GraphCopy& GC = dynamic_cast<const GraphCopy&>(AGC.constGraph());
			for (node vOrig : GA.constGraph().nodes) {
				node v = GC.copy(vOrig);
				if (v != nullptr) {
					AGC.height(v) = GA.height(vOrig);
					AGC.width(v) = GA.width(vOrig);
					AGC.shape(v) = GA.shape(vOrig);
				}
			}
		}

		doCall(levels, AGC);
		AGC.transferToOriginal(GA);
	}

#if 0
	/**
	 *\brief Computes a hierarchy layout of \p H in \p AG.
	 * @param H is the input hierarchy.
	 * @param AG is assigned the hierarchy layout.
	 */
	void call(Hierarchy& H, GraphAttributes &AG) {
		GraphAttributes AGC(H);
		doCall(H,AGC);
		HierarchyLayoutModule::dynLayerDistance(AGC, H);
		HierarchyLayoutModule::addBends(AGC, H);
		AGC.transferToOriginal(AG);
	}

	/**
	 * \brief Computes a hierarchy layout of \p H in \p AG.
	 * @param H is the input hierarchy.
	 * @param AG is assigned the hierarchy layout.
	 * @param AGC is GraphAttributes init. with H and AG
	 */
	void call(const Hierarchy& H, GraphAttributes &, GraphAttributes &AGC) {
		doCall(H,AGC);
	}

	//! Adds bends to edges for avoiding crossings with nodes.
	static void addBends(GraphAttributes &AGC, HierarchyLevels &levels);
#endif

	static void dynLayerDistance(GraphAttributes& AGC, HierarchyLevelsBase& levels);

private:
	//! after calling, ci (cj) contains the number of nodes of level i (j=i-1) which overlap the edge (s,t)
	static void overlap(GraphAttributes& AGC, HierarchyLevelsBase& levels, node s, node t, int i,
			int& ci, int& cj);

protected:
	//! Returns the \p GA width of node \p v or 0 if it is a dummy node in the hierarchy of \p levels.
	static inline double getWidth(const GraphAttributes& GA, const HierarchyLevelsBase& levels,
			node v) {
		const GraphCopy& GC = levels.hierarchy();
		return GC.isDummy(v) ? 0.0 : GA.width(v);
	}

	//! Returns the \p GA height of node \p v or 0 if it is a dummy node in the hierarchy of \p levels.
	static inline double getHeight(const GraphAttributes& GA, const HierarchyLevelsBase& levels,
			node v) {
		const GraphCopy& GC = levels.hierarchy();
		return GC.isDummy(v) ? 0.0 : GA.height(v);
	}

	/**
	 * \brief Implements the actual algorithm call.
	 *
	 * Must be implemented by derived classes.
	 *
	 * @param levels is the input hierarchy.
	 * @param AGC    has to be assigned the hierarchy layout.
	 */
	virtual void doCall(const HierarchyLevelsBase& levels, GraphAttributes& AGC) = 0;

	OGDF_MALLOC_NEW_DELETE
};

}
