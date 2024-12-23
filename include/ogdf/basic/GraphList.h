/** \file
 * \brief Decralation of GraphElement and GraphList classes.
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

#include <ogdf/basic/Array.h>
#include <ogdf/basic/basic.h>
#include <ogdf/basic/internal/config_autogen.h>
#include <ogdf/basic/internal/graph_iterators.h>
#include <ogdf/basic/memory.h>

#include <iterator>
#include <random>
#include <utility>

namespace ogdf {

class ClusterGraph;
class CombinatorialEmbedding;
class ConstCombinatorialEmbedding;
class Graph;

namespace internal {


//! The base class for objects used by (hyper)graphs.
/**
 * Such graph objects are maintained in list (see GraphList<T>),
 * and GraphElement basically provides a next and previous pointer
 * for these objects.
 */
class OGDF_EXPORT GraphElement {
	friend class ogdf::Graph;
	friend class GraphListBase;

protected:
	GraphElement* m_next = nullptr; //!< The successor in the list.
	GraphElement* m_prev = nullptr; //!< The predecessor in the list.

	OGDF_NEW_DELETE
};

//! Base class for GraphElement lists.
class OGDF_EXPORT GraphListBase {
protected:
	int m_size; //!< The size of the list.
	GraphElement* m_head; //!< Pointer to the first element in the list.
	GraphElement* m_tail; //!< Pointer to the last element in the list.

public:
	//! Constructs an empty list.
	GraphListBase() {
		m_head = m_tail = nullptr;
		m_size = 0;
	}

	//! Destruction
	~GraphListBase() { }

	//! Returns the size of the list.
	int size() const { return m_size; }

	//! Returns true iff the list is empty.
	bool empty() const { return m_size == 0; }

	//! Adds element \p pX at the end of the list.
	void pushBack(GraphElement* pX) {
		pX->m_next = nullptr;
		pX->m_prev = m_tail;
		if (m_head) {
			m_tail = m_tail->m_next = pX;
		} else {
			m_tail = m_head = pX;
		}
		++m_size;
	}

	//! Inserts element \p pX after element \p pY.
	void insertAfter(GraphElement* pX, GraphElement* pY) {
		pX->m_prev = pY;
		GraphElement* pYnext = pX->m_next = pY->m_next;
		pY->m_next = pX;
		if (pYnext) {
			pYnext->m_prev = pX;
		} else {
			m_tail = pX;
		}
		++m_size;
	}

	//! Inserts element \p pX before element \p pY.
	void insertBefore(GraphElement* pX, GraphElement* pY) {
		pX->m_next = pY;
		GraphElement* pYprev = pX->m_prev = pY->m_prev;
		pY->m_prev = pX;
		if (pYprev) {
			pYprev->m_next = pX;
		} else {
			m_head = pX;
		}
		++m_size;
	}

	//! Removes element \p pX from the list.
	void del(GraphElement* pX) {
		GraphElement *pxPrev = pX->m_prev, *pxNext = pX->m_next;

		if (pxPrev) {
			pxPrev->m_next = pxNext;
		} else {
			m_head = pxNext;
		}
		if (pxNext) {
			pxNext->m_prev = pxPrev;
		} else {
			m_tail = pxPrev;
		}
		m_size--;
	}

	//! Sorts the list according to \p newOrder.
	template<class LIST>
	void sort(const LIST& newOrder) {
		using std::begin;
		using std::end;
		sort(begin(newOrder), end(newOrder));
	}

	//! Sorts the list according to the range defined by two iterators.
	template<class IT>
	void sort(IT begin, IT end) {
		if (begin == end) {
			return;
		}
		m_head = *begin;
		GraphElement* pPred = nullptr;
		for (auto it = begin; it != end; ++it) {
			GraphElement* p = *it;
			if ((p->m_prev = pPred) != nullptr) {
				pPred->m_next = p;
			}
			pPred = p;
		}
		(m_tail = pPred)->m_next = nullptr;
	}

	//! Reverses the order of the list elements.
	void reverse() {
		GraphElement* pX = m_head;
		m_head = m_tail;
		m_tail = pX;
		while (pX) {
			GraphElement* pY = pX->m_next;
			pX->m_next = pX->m_prev;
			pX = pX->m_prev = pY;
		}
	}

	//! Exchanges the positions of \p pX and \p pY in the list.
	void swap(GraphElement* pX, GraphElement* pY) {
		if (pX->m_next == pY) {
			pX->m_next = pY->m_next;
			pY->m_prev = pX->m_prev;
			pY->m_next = pX;
			pX->m_prev = pY;

		} else if (pY->m_next == pX) {
			pY->m_next = pX->m_next;
			pX->m_prev = pY->m_prev;
			pX->m_next = pY;
			pY->m_prev = pX;

		} else {
			std::swap(pX->m_next, pY->m_next);
			std::swap(pX->m_prev, pY->m_prev);
		}

		if (pX->m_prev) {
			pX->m_prev->m_next = pX;
		} else {
			m_head = pX;
		}
		if (pX->m_next) {
			pX->m_next->m_prev = pX;
		} else {
			m_tail = pX;
		}

		if (pY->m_prev) {
			pY->m_prev->m_next = pY;
		} else {
			m_head = pY;
		}
		if (pY->m_next) {
			pY->m_next->m_prev = pY;
		} else {
			m_tail = pY;
		}

#ifdef OGDF_DEBUG
		consistencyCheck();
#endif
	}

	//! Permutes all list elements.
	template<class RNG>
	void permute(RNG& rng) {
		Array<GraphElement*> A(m_size + 2);
		A[0] = A[m_size + 1] = nullptr;

		int i = 1;
		GraphElement* pX;
		for (pX = m_head; pX; pX = pX->m_next) {
			A[i++] = pX;
		}

		A.permute(1, m_size, rng);

		for (i = 1; i <= m_size; i++) {
			pX = A[i];
			pX->m_next = A[i + 1];
			pX->m_prev = A[i - 1];
		}

		m_head = A[1];
		m_tail = A[m_size];

#ifdef OGDF_DEBUG
		consistencyCheck();
#endif
	}

	//! Permutes all list elements.
	void permute() {
		std::minstd_rand rng(randomSeed());
		permute(rng);
	}

#ifdef OGDF_DEBUG
	//! Asserts consistency of this list.
	void consistencyCheck() const {
		OGDF_ASSERT((m_head == nullptr) == (m_tail == nullptr));

		if (m_head != nullptr) {
			OGDF_ASSERT(m_head->m_prev == nullptr);
			OGDF_ASSERT(m_tail->m_next == nullptr);

			for (GraphElement* pX = m_head; pX; pX = pX->m_next) {
				if (pX->m_prev) {
					OGDF_ASSERT(pX->m_prev->m_next == pX);
				} else {
					OGDF_ASSERT(pX == m_head);
				}

				if (pX->m_next) {
					OGDF_ASSERT(pX->m_next->m_prev == pX);
				} else {
					OGDF_ASSERT(pX == m_tail);
				}
			}
		}
	}
#endif

	OGDF_NEW_DELETE
};

//! Lists of graph objects (like nodes, edges, etc.).
/**
 * The template type \a T must be a class derived from GraphElement.
 */
template<class T>
class GraphList : protected GraphListBase {
public:
	//! The value type (a pointer to a specific graph object)
	using value_type = T*;
	//! Provides a bidirectional iterator to an object in the container.
	using iterator = GraphIterator<T*>;
	//! Provides a bidirectional reverse iterator to an object in the container.
	using reverse_iterator = GraphReverseIterator<T*>;

	//! Constructs an empty list.
	GraphList() { }

	//! Destruction: deletes all elements
	~GraphList() {
		if (m_head) {
			OGDF_ALLOCATOR::deallocateList(sizeof(T), m_head, m_tail);
		}
	}

	using GraphListBase::empty;
	using GraphListBase::size;

	//! Returns the first element in the list.
	T* head() const { return static_cast<T*>(m_head); }

	//! Returns the last element in the list.
	T* tail() const { return static_cast<T*>(m_tail); }

	//! Adds element \p pX at the end of the list.
	void pushBack(T* pX) { GraphListBase::pushBack(pX); }

	//! Inserts element \p pX after element \p pY.
	void insertAfter(T* pX, T* pY) { GraphListBase::insertAfter(pX, pY); }

	//! Inserts element \p pX before element \p pY.
	void insertBefore(T* pX, T* pY) { GraphListBase::insertBefore(pX, pY); }

	//! Moves element \p pX to list \p L and inserts it before or after \p pY.
	void move(T* pX, GraphList<T>& L, T* pY, Direction dir) {
		GraphListBase::del(pX);
		if (dir == Direction::after) {
			L.insertAfter(pX, pY);
		} else {
			L.insertBefore(pX, pY);
		}
	}

	//! Moves element \p pX to list \p L and inserts it at the end.
	void move(T* pX, GraphList<T>& L) {
		GraphListBase::del(pX);
		L.pushBack(pX);
	}

	//! Moves element \p pX from its current position to a position after \p pY.
	void moveAfter(T* pX, T* pY) {
		GraphListBase::del(pX);
		insertAfter(pX, pY);
	}

	//! Moves element \p pX from its current position to a position before \p pY.
	void moveBefore(T* pX, T* pY) {
		GraphListBase::del(pX);
		insertBefore(pX, pY);
	}

	//! Removes element \p pX from the list and deletes it.
	void del(T* pX) {
		GraphListBase::del(pX);
		delete pX;
	}

	//! Only removes element \p pX from the list; does not delete it.
	void delPure(T* pX) { GraphListBase::del(pX); }

	//! Removes all elements from the list and deletes them.
	void clear() {
		if (m_head) {
			OGDF_ALLOCATOR::deallocateList(sizeof(T), m_head, m_tail);
			m_head = m_tail = nullptr;
			m_size = 0;
		}
	}

	//! Returns an iterator to the first element in the container.
	iterator begin() const { return GraphList<T>::head(); }

	//! Returns an iterator to the one-past-last element in the container.
	iterator end() const { return iterator(); }

	//! Returns a reverse iterator to the last element in the container.
	reverse_iterator rbegin() const { return reverse_iterator(GraphList<T>::tail()); }

	//! Returns a reverse iterator to the one-before-first element in the container.
	reverse_iterator rend() const { return reverse_iterator(); }

	using GraphListBase::permute;
	using GraphListBase::reverse;
	using GraphListBase::sort;

	//! Exchanges the positions of \p pX and \p pY in the list.
	void swap(T* pX, T* pY) { GraphListBase::swap(pX, pY); }

#ifdef OGDF_DEBUG
	using GraphListBase::consistencyCheck;
#endif
};

//! Public read-only interface for lists of graph objects.
template<class GraphObject>
class GraphObjectContainer : private GraphList<GraphObject> {
	friend class ogdf::Graph;
	friend class ogdf::ClusterGraph;
	friend class ogdf::ConstCombinatorialEmbedding;
	friend class ogdf::CombinatorialEmbedding;

public:
	using typename GraphList<GraphObject>::value_type;
	using typename GraphList<GraphObject>::iterator;
	using typename GraphList<GraphObject>::reverse_iterator;

	using GraphList<GraphObject>::begin;
	using GraphList<GraphObject>::rbegin;
	using GraphList<GraphObject>::end;
	using GraphList<GraphObject>::rend;

	using GraphList<GraphObject>::size;
	using GraphList<GraphObject>::empty;
	using GraphList<GraphObject>::head;
	using GraphList<GraphObject>::tail;
};

}
}
