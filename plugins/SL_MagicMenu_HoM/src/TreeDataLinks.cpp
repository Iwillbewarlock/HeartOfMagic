// =============================================================================
// TREE DATA LINKS - the tree's structural link walks (TreeData.h StructuralPath,
// StructuralNeighbours), moved out of TreeData.cpp unchanged (2026-11-04: the
// file passed the 600-line limit). Pure data, no game types.
// =============================================================================

#include "TreeData.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace TreeData
{
    std::vector<int> StructuralPath(const Tree& a_tree, int a_from, int a_to)
    {
        const auto count = static_cast<int>(a_tree.nodes.size());
        if (a_from < 0 || a_from >= count || a_to < 0 || a_to >= count) {
            return {};
        }
        const auto& school = a_tree.nodes[a_from].school;
        if (a_tree.nodes[a_to].school != school) {
            return {};
        }
        // Breadth first; each node's neighbours in index order, so the first parent found is the lowest index
        std::vector<int> parent(count, -1);
        std::vector<int> queue{ a_from };
        parent[a_from] = a_from;
        std::vector<int> next;
        for (std::size_t head = 0; head < queue.size() && parent[a_to] < 0; ++head) {
            const auto current = queue[head];
            next.clear();
            for (const auto e : a_tree.adjacent[current]) {
                const auto& edge = a_tree.edges[e];
                const auto other = edge.from == current ? edge.to : edge.from;
                if (edge.kind == EdgeKind::kStructural && parent[other] < 0 && a_tree.nodes[other].school == school) {
                    next.push_back(other);
                }
            }
            std::ranges::sort(next);
            for (const auto other : next) {
                if (parent[other] < 0) {
                    parent[other] = current;
                    queue.push_back(other);
                }
            }
        }
        if (parent[a_to] < 0) {
            return {};
        }
        std::vector<int> path{ a_to };
        while (path.back() != a_from) {
            path.push_back(parent[path.back()]);
        }
        std::ranges::reverse(path);
        return path;
    }

    std::vector<int> StructuralNeighbours(const Tree& a_tree, int a_center)
    {
        if (a_center < 0 || a_center >= static_cast<int>(a_tree.nodes.size())) {
            return {};
        }
        std::vector<int> out;
        for (const auto e : a_tree.adjacent[a_center]) {
            const auto& edge = a_tree.edges[e];
            if (edge.kind == EdgeKind::kStructural) {
                out.push_back(edge.from == a_center ? edge.to : edge.from);
            }
        }
        std::ranges::sort(out);
        const auto [first, last] = std::ranges::unique(out);
        out.erase(first, last);
        out.insert(out.begin(), a_center);
        return out;
    }
}
