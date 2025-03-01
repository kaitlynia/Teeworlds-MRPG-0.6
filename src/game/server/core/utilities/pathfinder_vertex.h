#ifndef GAME_SERVER_CORE_UTILITIES_PATHFINDER_VERTEX_H
#define GAME_SERVER_CORE_UTILITIES_PATHFINDER_VERTEX_H

// vertex graph
class PathFinderVertex
{
    int m_numVertices{};
    bool m_initilized{};
    std::list<int>* m_adjLists{};

    // Breadth-First Search Algorithm
    bool algoritmBFS(int startVertex, int endVertex, std::vector<int>& paPath) const
    {
        std::vector visited(m_numVertices, false);
        visited[startVertex] = true;

        std::queue<int> queue;
    	queue.push(startVertex);

        std::vector parent(m_numVertices, -1);
        while(!queue.empty())
        {
	        const int currVertex = queue.front();
            queue.pop();

            if(currVertex == endVertex)
            {
                int node = currVertex;

            	while(node != -1)
                {
                    paPath.push_back(node);
                    node = parent[node];
                }

                std::reverse(paPath.begin(), paPath.end());
                return true;
            }

            for(int adjVertex : m_adjLists[currVertex])
            {
                if(!visited[adjVertex])
                {
                    visited[adjVertex] = true;
                    queue.push(adjVertex);
                    parent[adjVertex] = currVertex;
                }
            }
        }

        return false;
    }

public:
    PathFinderVertex() = default;

	// initialize the graph with the given number of vertices
	void init(int vertices)
	{
		m_initilized = true;
		m_numVertices = vertices;
		m_adjLists = new std::list<int>[m_numVertices];
	}

	// add an edge between two vertices (undirected)
	void addEdge(int from, int to) const
	{
		m_adjLists[from].push_back(to);
		m_adjLists[to].push_back(from);
	}

	// find a path from startVertex to endVertex using BFS algorithm
	std::vector<int> findPath(int startVertex, int endVertex) const
	{
		std::vector<int> path;
		algoritmBFS(startVertex, endVertex, path);
		return path;
	}

	// check if the graph is initialized
	bool isInitilized() const { return m_initilized; }
};

#endif //GAME_SERVER_MMO_UTILS_TIME_PERIOD_DATA_H
