#include <iostream>
#include <string>
#include <cmath>
#include <ctime>

using namespace std;

// ============================================================
// CONSTANTS
// ============================================================

const int MAX_ZONES = 10;
const int GRID_ROWS = 3;
const int GRID_COLS = 4;
const int HASH_SIZE = 17;
const int MAX_QUEUE = 100;
const int MAX_GRAPH_NODES = 10;

const double TEMP_THRESHOLD = 45.0;
const double SMOKE_THRESHOLD = 70.0;
const double HUMIDITY_THRESHOLD = 20.0;

// ============================================================
// INPUT HELPER - Handles invalid/non-integer cin input
// ============================================================

// Reads a valid integer from cin, clears error state on bad input
int readInt(const string& prompt)
{
    int value;
    while (true)
    {
        cout << prompt;
        cin >> value;
        if (cin.fail())
        {
            cin.clear();
            cin.ignore(10000, '\n');
            cout << "Invalid input. Please enter a whole number." << endl;
        }
        else
        {
            cin.ignore(10000, '\n');
            return value;
        }
    }
}

// Reads a valid double from cin, clears error state on bad input
double readDouble(const string& prompt)
{
    double value;
    while (true)
    {
        cout << prompt;
        cin >> value;
        if (cin.fail())
        {
            cin.clear();
            cin.ignore(10000, '\n');
            cout << "Invalid input. Please enter a numeric value." << endl;
        }
        else
        {
            cin.ignore(10000, '\n');
            return value;
        }
    }
}

// Reads a valid integer within a given range [minVal, maxVal]
int readIntInRange(const string& prompt, int minVal, int maxVal)
{
    int value;
    while (true)
    {
        value = readInt(prompt);
        if (value < minVal || value > maxVal)
        {
            cout << "Input out of range. Please enter a value between " << minVal << " and " << maxVal << "." << endl;
        }
        else
        {
            return value;
        }
    }
}

// Reads a valid double within a given range [minVal, maxVal]
double readDoubleInRange(const string& prompt, double minVal, double maxVal)
{
    double value;
    while (true)
    {
        value = readDouble(prompt);
        if (value < minVal || value > maxVal)
        {
            cout << "Input out of range. Please enter a value between " << minVal << " and " << maxVal << "." << endl;
        }
        else
        {
            return value;
        }
    }
}

// ============================================================
// ARRAY-BASED ENVIRONMENTAL LAYER
// ============================================================

// Static baseline (normal conditions)
double staticTempBaseline[MAX_ZONES]     = {25, 25, 25, 25, 25, 25, 25, 25, 25, 25};
double staticSmokeBaseline[MAX_ZONES]    = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
double staticHumidityBaseline[MAX_ZONES] = {60, 60, 60, 60, 60, 60, 60, 60, 60, 60};

// Dynamic sensor arrays (live readings)
double dynamicTemp[MAX_ZONES]     = {25, 25, 25, 25, 25, 25, 25, 25, 25, 25};
double dynamicSmoke[MAX_ZONES]    = {0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
double dynamicHumidity[MAX_ZONES] = {60, 60, 60, 60, 60, 60, 60, 60, 60, 60};
bool   zoneActive[MAX_ZONES]      = {true, true, true, true, true, true, true, true, true, true};

// 2D forest grid matrix (temperature readings per cell)
double forestGrid[GRID_ROWS][GRID_COLS] = {
    {25, 30, 28, 26},
    {27, 45, 32, 29},
    {26, 29, 31, 25}
};

// ============================================================
// LINKED LIST - EVENT MEMORY LAYER
// ============================================================

struct EventNode
{
    double value;
    string type;   // "temperature", "smoke", "humidity"
    int zoneID;
    int timestamp;
    EventNode* next;
    EventNode* prev;
};

// Singly linked list (L1 - raw stream)
EventNode* rawEventHead = NULL;
int eventTimestamp = 0;

// Doubly linked list (L4/L5 - correction chain)
EventNode* correctionHead = NULL;
EventNode* correctionTail = NULL;

// Circular list (L7 - local monitoring loop)
EventNode* circularHead = NULL;
int circularSize = 0;

// Last stable state snapshot (for rollback)
double stableTemp[MAX_ZONES];
double stableSmoke[MAX_ZONES];
double stableHumidity[MAX_ZONES];
bool stableSnapshotExists = false;

// Adds a new event node to the singly linked raw event list
void addRawEvent(double val, string type, int zone)
{
    EventNode* newNode = new EventNode();
    newNode->value = val;
    newNode->type = type;
    newNode->zoneID = zone;
    newNode->timestamp = eventTimestamp++;
    newNode->next = NULL;
    newNode->prev = NULL;

    if (rawEventHead == NULL)
    {
        rawEventHead = newNode;
    }
    else
    {
        EventNode* temp = rawEventHead;
        // Traversal to tail for insertion - O(n) where n = number of events
        while (temp->next != NULL)
        {
            temp = temp->next;
        }
        temp->next = newNode;
    }
}

// Adds a node to the doubly linked correction chain
void addCorrectionEvent(double val, string type, int zone)
{
    EventNode* newNode = new EventNode();
    newNode->value = val;
    newNode->type = type;
    newNode->zoneID = zone;
    newNode->timestamp = eventTimestamp++;
    newNode->next = NULL;
    newNode->prev = correctionTail;

    // Tail pointer maintained - O(1) insert at doubly linked tail
    if (correctionTail != NULL)
    {
        correctionTail->next = newNode;
    }
    correctionTail = newNode;

    if (correctionHead == NULL)
    {
        correctionHead = newNode;
    }
}

// Adds a node to the circular monitoring loop
void addCircularEvent(double val, string type, int zone)
{
    EventNode* newNode = new EventNode();
    newNode->value = val;
    newNode->type = type;
    newNode->zoneID = zone;
    newNode->timestamp = eventTimestamp++;
    newNode->prev = NULL;

    if (circularHead == NULL)
    {
        newNode->next = newNode;
        circularHead = newNode;
    }
    else
    {
        EventNode* temp = circularHead;
        while (temp->next != circularHead)
        {
            temp = temp->next;
        }
        temp->next = newNode;
        newNode->next = circularHead;
    }
    circularSize++;
}

// Saves current sensor state as a stable snapshot for rollback
void saveStableSnapshot()
{
    for (int i = 0; i < MAX_ZONES; i++)
    {
        stableTemp[i]     = dynamicTemp[i];
        stableSmoke[i]    = dynamicSmoke[i];
        stableHumidity[i] = dynamicHumidity[i];
    }
    stableSnapshotExists = true;
    cout << "Stable snapshot saved successfully." << endl;
}

// Restores sensor state from last saved snapshot
void restoreStableSnapshot()
{
    if (!stableSnapshotExists)
    {
        cout << "No stable snapshot available to restore." << endl;
        return;
    }
    for (int i = 0; i < MAX_ZONES; i++)
    {
        dynamicTemp[i]     = stableTemp[i];
        dynamicSmoke[i]    = stableSmoke[i];
        dynamicHumidity[i] = stableHumidity[i];
    }
    cout << "System restored to last stable state." << endl;
}

// ============================================================
// QUEUE-BASED SCHEDULING ENGINE
// ============================================================

struct Task
{
    string description;
    int priority;  // 1 = low, 2 = medium, 3 = high/emergency
    int zoneID;
};

// Simple array-based queues
struct Queue
{
    Task tasks[MAX_QUEUE];
    int front;
    int rear;
    int size;
};

Queue routineQueue;
Queue surveillanceQueue;
Queue emergencyQueue;
Queue multiQueue;

// Initializes all queue instances
void initQueues()
{
    routineQueue.front     = 0; routineQueue.rear     = 0; routineQueue.size     = 0;
    surveillanceQueue.front= 0; surveillanceQueue.rear= 0; surveillanceQueue.size= 0;
    emergencyQueue.front   = 0; emergencyQueue.rear   = 0; emergencyQueue.size   = 0;
    multiQueue.front       = 0; multiQueue.rear       = 0; multiQueue.size       = 0;
}

// Enqueues a task into a given queue
void enqueue(Queue &q, Task t)
{
    if (q.size >= MAX_QUEUE)
    {
        cout << "Queue is full. Cannot add task." << endl;
        return;
    }
    // Circular array enqueue - O(1)
    q.tasks[q.rear] = t;
    q.rear = (q.rear + 1) % MAX_QUEUE;
    q.size++;
}

// Dequeues and returns the front task
Task dequeue(Queue &q)
{
    Task empty;
    empty.description = "EMPTY";
    empty.priority = 0;
    empty.zoneID = -1;

    if (q.size == 0)
    {
        cout << "Queue is empty." << endl;
        return empty;
    }

    // Circular array dequeue - O(1)
    Task t = q.tasks[q.front];
    q.front = (q.front + 1) % MAX_QUEUE;
    q.size--;
    return t;
}

// Enqueues into emergency queue sorted by priority (highest first)
void enqueuePriority(Queue &q, Task t)
{
    if (q.size >= MAX_QUEUE)
    {
        cout << "Priority queue is full." << endl;
        return;
    }
    q.tasks[q.rear] = t;
    q.rear = (q.rear + 1) % MAX_QUEUE;
    q.size++;


    // Insertion sort to maintain priority order - O(n) where n = queue size
    for (int i = q.size - 1; i > 0; i--)
    {
        int curr = (q.front + i) % MAX_QUEUE;
        int prev = (q.front + i - 1) % MAX_QUEUE;
        if (q.tasks[curr].priority > q.tasks[prev].priority)
        {
            Task temp = q.tasks[curr];
            q.tasks[curr] = q.tasks[prev];
            q.tasks[prev] = temp;
        }
        else
        {
            break;
        }
    }
}

// ============================================================
// TREE-BASED DECISION INTELLIGENCE LAYER
// ============================================================

struct TreeNode
{
    string label;
    double riskScore;
    TreeNode* left;
    TreeNode* right;
};

// Creates a new tree node with given label and risk
TreeNode* createTreeNode(string label, double risk)
{
    TreeNode* node = new TreeNode();
    node->label = label;
    node->riskScore = risk;
    node->left = NULL;
    node->right = NULL;
    return node;
}

// Builds a simple decision tree for fire classification
TreeNode* buildFireDecisionTree()
{
    TreeNode* root = createTreeNode("Forest Root", 0.0);
    root->left  = createTreeNode("Zone A - Normal", 0.2);
    root->right = createTreeNode("Zone B - High Risk", 0.8);
    root->left->left   = createTreeNode("Zone A1 - Safe", 0.1);
    root->left->right  = createTreeNode("Zone A2 - Monitor", 0.4);
    root->right->left  = createTreeNode("Zone B1 - Alert", 0.7);
    root->right->right = createTreeNode("Zone B2 - Emergency", 0.95);
    return root;
}

// Traverses decision tree in-order and prints each node's decision
void inorderDecision(TreeNode* node, int depth)
{
    if (node == NULL)
    {
        return;
    }

    // Inorder traversal visits all nodes - O(n) where n = number of tree nodes
    inorderDecision(node->left, depth + 1);
    for (int i = 0; i < depth; i++) cout << "  ";
    cout << ">> " << node->label << " | Risk: " << node->riskScore << endl;
    inorderDecision(node->right, depth + 1);
}

// Computes a decision risk score from weighted sensor inputs
double computeRiskScore(int zone)
{
    double w1 = 0.4, w2 = 0.3, w3 = 0.3;
    // Constant-time weighted formula evaluation - O(1)
    double fireSignal  = (dynamicTemp[zone]  > TEMP_THRESHOLD)        ? 1.0 : dynamicTemp[zone] / TEMP_THRESHOLD;
    double smokeSignal = (dynamicSmoke[zone] > SMOKE_THRESHOLD)       ? 1.0 : dynamicSmoke[zone] / SMOKE_THRESHOLD;
    double humSignal   = (dynamicHumidity[zone] < HUMIDITY_THRESHOLD) ? 1.0 : 1.0 - (dynamicHumidity[zone] / 100.0);
    return w1 * fireSignal + w2 * smokeSignal + w3 * humSignal;
}

// ============================================================
// GRAPH-BASED ROUTING LAYER
// ============================================================

struct AdjNode
{
    int dest;
    double cost;
    AdjNode* next;
};

AdjNode* adjList[MAX_GRAPH_NODES];
int numZones = 6;

double adjMatrix[MAX_GRAPH_NODES][MAX_GRAPH_NODES];
double fireLevel[MAX_GRAPH_NODES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Initializes adjacency list and matrix
void initGraph()
{
    for (int i = 0; i < MAX_GRAPH_NODES; i++)
    {
        adjList[i] = NULL;
        for (int j = 0; j < MAX_GRAPH_NODES; j++)
        {
            adjMatrix[i][j] = 0;
        }
    }
}

// Adds a directed edge to adjacency list
void addEdgeList(int src, int dest, double cost)
{
    AdjNode* newNode = new AdjNode();
    newNode->dest = dest;
    newNode->cost = cost;
    newNode->next = adjList[src];
    adjList[src] = newNode;
}

// Adds an undirected edge to adjacency matrix
void addEdgeMatrix(int src, int dest, double cost)
{
    adjMatrix[src][dest] = cost;
    adjMatrix[dest][src] = cost;
}

// Loads a default forest graph with zones 0-5
void loadDefaultGraph()
{
    initGraph();
    numZones = 6;

    addEdgeList(0, 1, 5); addEdgeList(0, 2, 3);
    addEdgeList(1, 3, 4); addEdgeList(2, 3, 6);
    addEdgeList(3, 4, 2); addEdgeList(3, 5, 7);
    addEdgeList(4, 5, 3);

    addEdgeMatrix(0, 1, 5); addEdgeMatrix(0, 2, 3);
    addEdgeMatrix(1, 3, 4); addEdgeMatrix(2, 3, 6);
    addEdgeMatrix(3, 4, 2); addEdgeMatrix(3, 5, 7);
    addEdgeMatrix(4, 5, 3);

    cout << "Default forest graph loaded (6 zones, 7 paths)." << endl;
}

// BFS traversal from a start zone - used for fire spread prediction
void bfsTraversal(int start)
{
    bool visited[MAX_GRAPH_NODES] = {false};
    int bfsQueue[MAX_GRAPH_NODES];
    int front = 0, rear = 0;

    visited[start] = true;
    bfsQueue[rear++] = start;

    cout << "BFS Fire Spread from Zone " << start << ": ";

    // BFS traversal - O(V + E) where V = zones, E = paths
    while (front < rear)
    {
        int current = bfsQueue[front++];
        cout << "Zone" << current << " ";

        AdjNode* temp = adjList[current];
        while (temp != NULL)
        {
            if (!visited[temp->dest])
            {
                visited[temp->dest] = true;
                bfsQueue[rear++] = temp->dest;
            }
            temp = temp->next;
        }
    }
    cout << endl;
}

// DFS helper for recursive traversal
void dfsHelper(int node, bool visited[])
{
    // DFS traversal - O(V + E) where V = zones, E = paths
    visited[node] = true;
    cout << "Zone" << node << " ";

    AdjNode* temp = adjList[node];
    while (temp != NULL)
    {
        if (!visited[temp->dest])
        {
            dfsHelper(temp->dest, visited);
        }
        temp = temp->next;
    }
}

// DFS traversal from a start zone - used for deep path analysis
void dfsTraversal(int start)
{
    bool visited[MAX_GRAPH_NODES] = {false};
    cout << "DFS Deep Analysis from Zone " << start << ": ";
    dfsHelper(start, visited);
    cout << endl;
}

// Computes safe path cost between two zones using fire-aware formula
void computeSafePath(int src, int dest)
{
    bool visited[MAX_GRAPH_NODES] = {false};
    int path[MAX_GRAPH_NODES];
    int pathLen = 0;
    double totalCost = 0;

    int current = src;
    visited[current] = true;
    path[pathLen++] = current;

    cout << "Safe path from Zone" << src << " to Zone" << dest << ":" << endl;

    // Greedy fire-aware path - O(V * E) worst case
    while (current != dest)
    {
        AdjNode* temp = adjList[current];
        int nextZone = -1;
        double minCost = 9999;

        while (temp != NULL)
        {
            if (!visited[temp->dest])
            {
                double fireCost = temp->cost * (1.0 + fireLevel[temp->dest]);
                if (fireCost < minCost)
                {
                    minCost = fireCost;
                    nextZone = temp->dest;
                }
            }
            temp = temp->next;
        }

        if (nextZone == -1)
        {
            cout << "No path found (blocked by fire or disconnected)." << endl;
            return;
        }

        visited[nextZone] = true;
        path[pathLen++] = nextZone;
        totalCost += minCost;
        current = nextZone;
    }

    cout << "Path: ";
    for (int i = 0; i < pathLen; i++)
    {
        cout << "Zone" << path[i];
        if (i < pathLen - 1) cout << " -> ";
    }
    cout << endl;
    cout << "Total fire-aware cost: " << totalCost << endl;
}

// ============================================================
// HASH-BASED INDEXING LAYER
// ============================================================

struct HashEntry
{
    int key;
    double temp;
    double smoke;
    double humidity;
    bool occupied;
    HashEntry* chainNext;
};

HashEntry hashTable[HASH_SIZE];
HashEntry cacheTable[HASH_SIZE];

// Initializes hash tables
void initHashTables()
{
    for (int i = 0; i < HASH_SIZE; i++)
    {
        hashTable[i].occupied = false;
        hashTable[i].chainNext = NULL;
        cacheTable[i].occupied = false;
        cacheTable[i].chainNext = NULL;
    }
}

// Hash function
int hashFunction(int key)
{
    return key % HASH_SIZE;
}

// Inserts zone data into hash table with chaining for collisions
void hashInsert(int zoneID)
{
    // Hash function index = key % HASH_SIZE - O(1) average, O(n) worst with chain
    int index = hashFunction(zoneID);

    if (!hashTable[index].occupied)
    {
        hashTable[index].key      = zoneID;
        hashTable[index].temp     = dynamicTemp[zoneID % MAX_ZONES];
        hashTable[index].smoke    = dynamicSmoke[zoneID % MAX_ZONES];
        hashTable[index].humidity = dynamicHumidity[zoneID % MAX_ZONES];
        hashTable[index].occupied = true;
        hashTable[index].chainNext = NULL;
        cout << "Zone " << zoneID << " inserted at index " << index << endl;
    }
    else
    {
        HashEntry* newEntry = new HashEntry();
        newEntry->key      = zoneID;
        newEntry->temp     = dynamicTemp[zoneID % MAX_ZONES];
        newEntry->smoke    = dynamicSmoke[zoneID % MAX_ZONES];
        newEntry->humidity = dynamicHumidity[zoneID % MAX_ZONES];
        newEntry->occupied = true;
        newEntry->chainNext = NULL;

        HashEntry* current = &hashTable[index];
        while (current->chainNext != NULL)
        {
            current = current->chainNext;
        }
        current->chainNext = newEntry;
        cout << "Collision at index " << index << ". Zone " << zoneID << " chained." << endl;
    }
}

// Retrieves zone data from hash table
void hashRetrieve(int zoneID)
{
    int index = hashFunction(zoneID);
    // O(1) average lookup, O(n) worst case on collision chain
    HashEntry* current = &hashTable[index];

    while (current != NULL && current->occupied)
    {
        if (current->key == zoneID)
        {
            cout << "Zone " << zoneID << " found at index " << index << ":" << endl;
            cout << "  Temperature : " << current->temp << endl;
            cout << "  Smoke       : " << current->smoke << endl;
            cout << "  Humidity    : " << current->humidity << endl;

            int cacheIdx = hashFunction(zoneID);
            cacheTable[cacheIdx] = *current;
            return;
        }
        current = current->chainNext;
    }
    cout << "Zone " << zoneID << " not found in hash table." << endl;
}

// Updates cache with latest data for a zone
void updateCache(int zoneID)
{
    int index = hashFunction(zoneID);
    cacheTable[index].key      = zoneID;
    cacheTable[index].temp     = dynamicTemp[zoneID % MAX_ZONES];
    cacheTable[index].smoke    = dynamicSmoke[zoneID % MAX_ZONES];
    cacheTable[index].humidity = dynamicHumidity[zoneID % MAX_ZONES];
    cacheTable[index].occupied = true;
    cout << "Cache updated for Zone " << zoneID << " at index " << index << endl;
}

// Displays all occupied entries in hash and cache tables
void viewIndexTable()
{
    cout << "--- Primary Hash Table ---" << endl;
    for (int i = 0; i < HASH_SIZE; i++)
    {
        if (hashTable[i].occupied)
        {
            cout << "Index " << i << ": Zone " << hashTable[i].key
                 << " | Temp=" << hashTable[i].temp
                 << " Smoke=" << hashTable[i].smoke
                 << " Humidity=" << hashTable[i].humidity << endl;

            HashEntry* chain = hashTable[i].chainNext;
            while (chain != NULL)
            {
                cout << "  (chain) Zone " << chain->key
                     << " | Temp=" << chain->temp
                     << " Smoke=" << chain->smoke
                     << " Humidity=" << chain->humidity << endl;
                chain = chain->chainNext;
            }
        }
    }
    cout << "--- Cache Table ---" << endl;
    for (int i = 0; i < HASH_SIZE; i++)
    {
        if (cacheTable[i].occupied)
        {
            cout << "Cache[" << i << "]: Zone " << cacheTable[i].key
                 << " | Temp=" << cacheTable[i].temp
                 << " Smoke=" << cacheTable[i].smoke << endl;
        }
    }
}

// ============================================================
// SYSTEM MONITORING LAYER
// ============================================================

struct SystemMetrics
{
    int totalTasksProcessed;
    int emergencyCount;
    int anomalyCount;
    double avgQueueLoad;
};

SystemMetrics sysMetrics = {0, 0, 0, 0.0};

// Displays current system health and load metrics
void monitorSystemLoad()
{
    cout << "=== System Load Monitor ===" << endl;
    cout << "Routine Queue      : " << routineQueue.size << " tasks" << endl;
    cout << "Surveillance Queue : " << surveillanceQueue.size << " tasks" << endl;
    cout << "Emergency Queue    : " << emergencyQueue.size << " tasks" << endl;
    cout << "Multi-Factor Queue : " << multiQueue.size << " tasks" << endl;

    double total = routineQueue.size + surveillanceQueue.size + emergencyQueue.size + multiQueue.size;
    cout << "Total Queue Load   : " << total << " tasks" << endl;

    if (total > 20)
    {
        cout << "WARNING: System overload detected. Consider load redistribution." << endl;
    }
    else
    {
        cout << "System load is within normal range." << endl;
    }
}

// Checks for bottlenecks across all subsystems
void detectBottlenecks()
{
    cout << "=== Bottleneck Detection ===" << endl;
    if (emergencyQueue.size > 10)
    {
        cout << "BOTTLENECK: Emergency queue has " << emergencyQueue.size << " unprocessed tasks." << endl;
    }
    if (routineQueue.size > 30)
    {
        cout << "BOTTLENECK: Routine queue overloaded with " << routineQueue.size << " tasks." << endl;
    }

    int highRiskZones = 0;
    for (int i = 0; i < MAX_ZONES; i++)
    {
        if (dynamicTemp[i] > TEMP_THRESHOLD || dynamicSmoke[i] > SMOKE_THRESHOLD)
        {
            highRiskZones++;
        }
    }

    if (highRiskZones > 3)
    {
        cout << "BOTTLENECK: " << highRiskZones << " zones in high-risk state. Graph routing under pressure." << endl;
    }
    else
    {
        cout << "No critical bottlenecks detected." << endl;
    }
}

// Displays an overall health summary of the system
void viewSystemHealth()
{
    cout << "=== System Health Report ===" << endl;
    cout << "Tasks Processed  : " << sysMetrics.totalTasksProcessed << endl;
    cout << "Emergency Events : " << sysMetrics.emergencyCount << endl;
    cout << "Anomalies Logged : " << sysMetrics.anomalyCount << endl;

    int activeZones = 0;
    for (int i = 0; i < MAX_ZONES; i++)
    {
        if (zoneActive[i]) activeZones++;
    }
    cout << "Active Zones     : " << activeZones << "/" << MAX_ZONES << endl;

    if (sysMetrics.emergencyCount > 5)
    {
        cout << "System Status    : CRITICAL" << endl;
    }
    else if (sysMetrics.emergencyCount > 2)
    {
        cout << "System Status    : WARNING" << endl;
    }
    else
    {
        cout << "System Status    : NORMAL" << endl;
    }
}

// ============================================================
// HELPER / DISPLAY FUNCTIONS
// ============================================================

// Validates sensor reading against physical bounds
bool isValidReading(double value, string type)
{
    if (type == "temperature")
    {
        if (value < 0 || value > 100) return false;
    }
    else if (type == "smoke")
    {
        if (value < 0 || value > 100) return false;
    }
    else if (type == "humidity")
    {
        if (value < 0 || value > 100) return false;
    }
    return true;
}

// Checks if a zone's current readings are anomalous
bool isAnomaly(int zone)
{
    if (dynamicTemp[zone] > TEMP_THRESHOLD)         return true;
    if (dynamicSmoke[zone] > SMOKE_THRESHOLD)       return true;
    if (dynamicHumidity[zone] < HUMIDITY_THRESHOLD) return true;
    return false;
}

// Performs spatial interpolation for a failed zone using neighbors
double interpolateValue(double top, double bottom, double left, double right)
{
    return (top + bottom + left + right) / 4.0;
}

// Displays the 1D time-series for temperature across all zones
void display1DTimeSeries()
{
    cout << "=== 1D Temperature Time Series (Zone 0 to " << MAX_ZONES - 1 << ") ===" << endl;
    // Compares baseline vs dynamic arrays - O(n) where n = MAX_ZONES
    for (int i = 0; i < MAX_ZONES; i++)
    {
        cout << "Zone " << i << ": Temp=" << dynamicTemp[i]
             << " Smoke=" << dynamicSmoke[i]
             << " Humidity=" << dynamicHumidity[i];
        if (!zoneActive[i]) cout << " [INACTIVE]";
        cout << endl;
    }
}

// Displays the 2D forest grid matrix
void display2DGrid()
{
    cout << "=== 2D Forest Zone Grid (Temperature) ===" << endl;
    for (int i = 0; i < GRID_ROWS; i++)
    {
        for (int j = 0; j < GRID_COLS; j++)
        {
            cout << forestGrid[i][j] << "\t";
        }
        cout << endl;
    }
}

// Displays zone-wise condition summary with risk level
void displayZoneConditions()
{
    cout << "=== Zone-wise Condition Summary ===" << endl;
    for (int i = 0; i < MAX_ZONES; i++)
    {
        double risk = computeRiskScore(i);
        cout << "Zone " << i
             << " | Temp=" << dynamicTemp[i]
             << " Smoke=" << dynamicSmoke[i]
             << " Humidity=" << dynamicHumidity[i]
             << " | Risk=" << risk;

        if (risk > 0.6)      cout << " [HIGH RISK]";
        else if (risk > 0.3) cout << " [MODERATE]";
        else                  cout << " [NORMAL]";

        if (!zoneActive[i]) cout << " [SENSOR FAILURE]";
        cout << endl;
    }
}

// Traverses and prints the raw singly linked event list forward
void traverseForward()
{
    if (rawEventHead == NULL)
    {
        cout << "No events recorded yet." << endl;
        return;
    }
    cout << "=== Forward Event Traversal ===" << endl;
    EventNode* temp = rawEventHead;
    while (temp != NULL)
    {
        cout << "[T=" << temp->timestamp << "] Zone" << temp->zoneID
             << " | " << temp->type << " = " << temp->value << endl;
        temp = temp->next;
    }
}

// Traverses and prints the doubly linked correction chain backward
void traverseBackward()
{
    if (correctionTail == NULL)
    {
        cout << "No correction events recorded yet." << endl;
        return;
    }
    cout << "=== Backward Event Traversal (Correction Chain) ===" << endl;
    EventNode* temp = correctionTail;
    while (temp != NULL)
    {
        cout << "[T=" << temp->timestamp << "] Zone" << temp->zoneID
             << " | " << temp->type << " = " << temp->value << endl;
        temp = temp->prev;
    }
}

// Runs a circular monitoring loop for a set number of cycles
void runCircularMonitoring()
{
    if (circularHead == NULL)
    {
        cout << "No events in circular monitor loop." << endl;
        return;
    }
    int cycles = 2;
    cout << "=== Circular Monitoring Loop (" << cycles << " cycles) ===" << endl;
    EventNode* temp = circularHead;
    for (int c = 0; c < cycles; c++)
    {
        cout << "-- Cycle " << c + 1 << " --" << endl;
        for (int i = 0; i < circularSize; i++)
        {
            cout << "Zone" << temp->zoneID << ": " << temp->type << "=" << temp->value << endl;
            temp = temp->next;
        }
    }
}

// ============================================================
// SCENARIO FUNCTIONS
// ============================================================

// Scenario 1: Cascading fire starts in Zone 3, spreads to 4 and 6
void scenarioCascadingFire()
{
    cout << endl << "=== SCENARIO 1: Cascading Fire & Resource Conflict ===" << endl;

    cout << "\n[Step 1] Fire detected in Zone 3. Injecting sensor data..." << endl;
    dynamicTemp[3]     = 68.0;
    dynamicSmoke[3]    = 85.0;
    dynamicHumidity[3] = 15.0;
    addRawEvent(68.0, "temperature", 3);
    addRawEvent(85.0, "smoke", 3);

    cout << "\n[Step 2] Saving stable state before fire escalation..." << endl;
    saveStableSnapshot();

    cout << "\n[Step 3] Fire spreading to Zone 4 and Zone 6..." << endl;
    dynamicTemp[4]  = 52.0; dynamicSmoke[4]  = 75.0;
    dynamicTemp[6]  = 48.0; dynamicSmoke[6]  = 60.0;
    addRawEvent(52.0, "temperature", 4);
    addRawEvent(48.0, "temperature", 6);

    cout << "\n[Step 4] Risk assessment:" << endl;
    int affectedZones[] = {3, 4, 6};
    for (int i = 0; i < 3; i++)
    {
        int z = affectedZones[i];
        double risk = computeRiskScore(z);
        cout << "Zone " << z << " Risk Score: " << risk;
        if (risk > 0.6) cout << " --> EMERGENCY RESPONSE TRIGGERED";
        cout << endl;
    }

    cout << "\n[Step 5] BFS fire spread simulation from Zone 3:" << endl;
    fireLevel[3] = 0.8;
    fireLevel[4] = 0.5;
    bfsTraversal(3);

    cout << "\n[Step 6] Queuing emergency response tasks..." << endl;
    Task t1; t1.description = "Deploy firefighters to Zone 3"; t1.priority = 3; t1.zoneID = 3;
    Task t2; t2.description = "Monitor Zone 4 perimeter";      t2.priority = 2; t2.zoneID = 4;
    Task t3; t3.description = "Evacuate Zone 6 residents";     t3.priority = 3; t3.zoneID = 6;
    enqueuePriority(emergencyQueue, t1);
    enqueuePriority(emergencyQueue, t2);
    enqueuePriority(emergencyQueue, t3);
    sysMetrics.emergencyCount += 3;

    cout << "Emergency tasks queued. Processing top priority..." << endl;
    Task top = dequeue(emergencyQueue);
    cout << "Processing: " << top.description << " (Priority=" << top.priority << ")" << endl;
    sysMetrics.totalTasksProcessed++;

    cout << "\n[Scenario 1 Complete] Fire contained. System state logged." << endl;
}

// Scenario 2: Sensor failure in Zone 2 with system reconstruction
void scenarioSensorFailure()
{
    cout << endl << "=== SCENARIO 2: Sensor Failure & System Reconstruction ===" << endl;

    cout << "\n[Step 1] Sensors in Zone 2 reporting invalid data..." << endl;
    zoneActive[2] = false;
    dynamicTemp[2]  = -999;
    dynamicSmoke[2] = -999;
    cout << "Zone 2 sensors marked as INACTIVE." << endl;
    sysMetrics.anomalyCount++;

    cout << "\n[Step 2] Attempting rollback to last stable state..." << endl;
    restoreStableSnapshot();

    cout << "\n[Step 3] Estimating Zone 2 values via spatial interpolation..." << endl;
    double top    = dynamicTemp[0];
    double bottom = dynamicTemp[5];
    double left   = dynamicTemp[1];
    double right  = dynamicTemp[3];
    double estimated = interpolateValue(top, bottom, left, right);
    dynamicTemp[2] = estimated;

    cout << "Interpolated temperature for Zone 2: " << estimated << "C" << endl;
    cout << "Using neighbors: top=" << top << " bottom=" << bottom
         << " left=" << left << " right=" << right << endl;

    dynamicSmoke[2]    = staticSmokeBaseline[2];
    dynamicHumidity[2] = staticHumidityBaseline[2];
    zoneActive[2] = true;

    cout << "\n[Step 4] Zone 2 reconstructed:" << endl;
    cout << "  Temp=" << dynamicTemp[2] << " Smoke=" << dynamicSmoke[2]
         << " Humidity=" << dynamicHumidity[2] << endl;

    addCorrectionEvent(estimated, "temperature", 2);
    cout << "\n[Step 5] Correction event logged in doubly linked chain." << endl;

    cout << "\n[Scenario 2 Complete] Zone 2 restored to normal operation." << endl;
}

// Scenario 3: Multi-factor anomaly from wildlife, fire, and human activity
void scenarioMultiFactorAnomaly()
{
    cout << endl << "=== SCENARIO 3: Multi-Factor Anomaly Escalation ===" << endl;

    cout << "\n[Step 1] Multiple anomalies detected simultaneously..." << endl;
    dynamicTemp[7]  = 47.0; dynamicSmoke[7]  = 55.0;
    dynamicTemp[8]  = 30.0; dynamicSmoke[8]  = 20.0;
    dynamicTemp[9]  = 28.0; dynamicHumidity[9] = 18.0;

    cout << "Zone 7: Fire anomaly (Temp=" << dynamicTemp[7] << ")" << endl;
    cout << "Zone 8: Wildlife anomaly" << endl;
    cout << "Zone 9: Human activity + low humidity" << endl;
    sysMetrics.anomalyCount += 3;

    cout << "\n[Step 2] Computing combined risk scores..." << endl;
    int checkZones[] = {7, 8, 9};
    for (int i = 0; i < 3; i++)
    {
        int z = checkZones[i];
        double risk = computeRiskScore(z);
        cout << "Zone " << z << " combined risk: " << risk;
        if (risk > 0.6) cout << " --> ESCALATING";
        cout << endl;

        if (risk > 0.6)
        {
            Task t;
            t.description = "Escalation alert for Zone " + to_string(z);
            t.priority = 3;
            t.zoneID = z;
            enqueuePriority(emergencyQueue, t);
            sysMetrics.emergencyCount++;
        }
    }

    cout << "\n[Step 3] DFS spread analysis from Zone 3:" << endl;
    dfsTraversal(3);

    addRawEvent(47.0, "temperature", 7);
    addRawEvent(18.0, "humidity", 9);
    addCircularEvent(55.0, "smoke", 7);

    cout << "\n[Step 4] Anomaly events logged in raw and circular streams." << endl;

    cout << "\n[Scenario 3 Complete] Multi-factor anomalies captured and escalated." << endl;
}

// Scenario 4: System overload - too many events arriving at once
void scenarioSystemOverload()
{
    cout << endl << "=== SCENARIO 4: System Overload & Load Redistribution ===" << endl;

    cout << "\n[Step 1] Flooding system with 15 simultaneous sensor updates..." << endl;
    for (int i = 0; i < 15; i++)
    {
        Task t;
        t.description = "Sensor update from Zone " + to_string(i % MAX_ZONES);
        t.priority = 1;
        t.zoneID = i % MAX_ZONES;
        enqueue(routineQueue, t);
    }
    cout << "Routine queue size: " << routineQueue.size << endl;

    cout << "\n[Step 2] Detecting overload..." << endl;
    monitorSystemLoad();

    cout << "\n[Step 3] Separating critical tasks from routine queue..." << endl;
    Task emergency;
    emergency.description = "CRITICAL: Overload fire alert in Zone 1";
    emergency.priority = 3;
    emergency.zoneID = 1;
    enqueuePriority(emergencyQueue, emergency);
    sysMetrics.emergencyCount++;

    cout << "\n[Step 4] Processing and draining routine tasks..." << endl;
    int processed = 0;
    while (routineQueue.size > 5 && processed < 10)
    {
        Task t = dequeue(routineQueue);
        sysMetrics.totalTasksProcessed++;
        processed++;
    }
    cout << "Processed " << processed << " routine tasks. Remaining: " << routineQueue.size << endl;

    cout << "\n[Step 5] Updating cache for high-access zones..." << endl;
    for (int z = 0; z < 3; z++)
    {
        updateCache(z);
    }

    cout << "\n[Step 6] Final bottleneck check:" << endl;
    detectBottlenecks();

    cout << "\n[Scenario 4 Complete] Load redistributed. System returning to normal." << endl;
}

// Scenario 5: Global multi-zone emergency synchronization
void scenarioGlobalEmergency()
{
    cout << endl << "=== SCENARIO 5: Global Multi-Zone Emergency Synchronization ===" << endl;

    cout << "\n[Step 1] Global emergency - multiple zones critical..." << endl;
    int emergencyZones[] = {0, 1, 3, 4, 5};
    for (int i = 0; i < 5; i++)
    {
        int z = emergencyZones[i];
        dynamicTemp[z]     = 55.0 + i * 3;
        dynamicSmoke[z]    = 80.0 + i * 2;
        dynamicHumidity[z] = 12.0;
        addRawEvent(dynamicTemp[z], "temperature", z);
        addCorrectionEvent(dynamicSmoke[z], "smoke", z);
    }

    cout << "\n[Step 2] Global risk assessment:" << endl;
    double totalRisk = 0;
    for (int i = 0; i < MAX_ZONES; i++)
    {
        double risk = computeRiskScore(i);
        totalRisk += risk;
        cout << "Zone " << i << " risk: " << risk;
        if (risk > 0.6) cout << " [CRITICAL]";
        cout << endl;
    }
    cout << "Total system risk sum: " << totalRisk << endl;

    if (totalRisk > 4.0)
    {
        cout << "GLOBAL ALERT ACTIVATED." << endl;
        sysMetrics.emergencyCount += 5;
    }

    cout << "\n[Step 3] Global BFS fire spread analysis:" << endl;
    for (int i = 0; i < 5; i++) fireLevel[i] = 0.7;
    bfsTraversal(0);

    cout << "\n[Step 4] Deploying synchronized global response tasks..." << endl;
    for (int i = 0; i < 5; i++)
    {
        Task t;
        t.description = "Global response: Zone " + to_string(emergencyZones[i]);
        t.priority = 3;
        t.zoneID = emergencyZones[i];
        enqueuePriority(emergencyQueue, t);
    }

    cout << "\n[Step 5] Decision tree evaluation:" << endl;
    TreeNode* tree = buildFireDecisionTree();
    inorderDecision(tree, 0);

    cout << "\n[Step 6] Post-emergency system health:" << endl;
    viewSystemHealth();

    cout << "\n[Scenario 5 Complete] Global response synchronized across all zones." << endl;
}

// Runs all 5 scenarios in sequence as a full simulation
void runFullSimulation()
{
    cout << "\n=============================" << endl;
    cout << "  FULL SYSTEM SIMULATION" << endl;
    cout << "=============================" << endl;
    loadDefaultGraph();
    scenarioCascadingFire();
    scenarioSensorFailure();
    scenarioMultiFactorAnomaly();
    scenarioSystemOverload();
    scenarioGlobalEmergency();
    cout << "\n=============================" << endl;
    cout << "  SIMULATION COMPLETE" << endl;
    cout << "=============================" << endl;
}

// ============================================================
// MENU FUNCTIONS
// ============================================================

void menuInputEnvironmentalData()
{
    cout << "\n--- 1. Input Environmental Data ---" << endl;
    cout << "1.1 Add Sensor Reading" << endl;
    cout << "1.2 Store Data in Dynamic Array" << endl;
    cout << "1.3 Compare with Static Baseline" << endl;
    cout << "1.4 Validate and Filter Noise" << endl;

    int choice = readIntInRange("Enter choice (1-4): ", 1, 4);

    if (choice == 1)
    {
        int zone = readIntInRange("Enter Zone ID (0-9): ", 0, MAX_ZONES - 1);
        double temp     = readDoubleInRange("Enter Temperature (0-100): ", 0, 100);
        double smoke    = readDoubleInRange("Enter Smoke Level (0-100): ", 0, 100);
        double humidity = readDoubleInRange("Enter Humidity (0-100): ", 0, 100);

        addRawEvent(temp, "temperature", zone);
        addRawEvent(smoke, "smoke", zone);
        addRawEvent(humidity, "humidity", zone);
        cout << "Sensor readings stored in raw event stream." << endl;
    }
    else if (choice == 2)
    {
        int zone = readIntInRange("Enter Zone ID (0-9): ", 0, MAX_ZONES - 1);
        double temp     = readDoubleInRange("Enter Temperature (0-100): ", 0, 100);
        double smoke    = readDoubleInRange("Enter Smoke Level (0-100): ", 0, 100);
        double humidity = readDoubleInRange("Enter Humidity (0-100): ", 0, 100);

        // Direct index read/write - O(1)
        dynamicTemp[zone]     = temp;
        dynamicSmoke[zone]    = smoke;
        dynamicHumidity[zone] = humidity;
        cout << "Dynamic array updated for Zone " << zone << endl;
    }
    else if (choice == 3)
    {
        cout << "=== Baseline vs Current Comparison ===" << endl;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            cout << "Zone " << i << ": Temp(" << staticTempBaseline[i] << " -> " << dynamicTemp[i] << ")"
                 << " Smoke(" << staticSmokeBaseline[i] << " -> " << dynamicSmoke[i] << ")"
                 << " Humidity(" << staticHumidityBaseline[i] << " -> " << dynamicHumidity[i] << ")" << endl;
        }
    }
    else if (choice == 4)
    {
        cout << "=== Noise Filtering & Anomaly Detection ===" << endl;
        int filtered = 0;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            if (!isValidReading(dynamicTemp[i], "temperature"))
            {
                cout << "Zone " << i << ": Invalid temperature " << dynamicTemp[i] << " filtered." << endl;
                dynamicTemp[i] = staticTempBaseline[i];
                filtered++;
            }
            if (isAnomaly(i))
            {
                cout << "Zone " << i << ": ANOMALY detected." << endl;
                sysMetrics.anomalyCount++;
            }
        }
        cout << "Filtering complete. " << filtered << " values corrected." << endl;
    }
}

void menuViewForestGrid()
{
    cout << "\n--- 2. View Forest Grid Status ---" << endl;
    cout << "2.1 Display 1D Time Series" << endl;
    cout << "2.2 Display 2D Grid Matrix" << endl;
    cout << "2.3 View Zone-wise Conditions" << endl;

    int choice = readIntInRange("Enter choice (1-3): ", 1, 3);

    if (choice == 1)      display1DTimeSeries();
    else if (choice == 2) display2DGrid();
    else if (choice == 3) displayZoneConditions();
}

void menuEventMemory()
{
    cout << "\n--- 3. Event Memory System ---" << endl;
    cout << "3.1 Store Event (Linked List)" << endl;
    cout << "3.2 Traverse Forward" << endl;
    cout << "3.3 Traverse Backward" << endl;
    cout << "3.4 Circular Event Monitoring" << endl;
    cout << "3.5 Restore Last Stable State" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        int zone = readIntInRange("Enter zone ID (0-9): ", 0, MAX_ZONES - 1);

        cout << "Enter type (1=temperature, 2=smoke, 3=humidity): ";
        int typeChoice = readIntInRange("", 1, 3);
        string type;
        if (typeChoice == 1) type = "temperature";
        else if (typeChoice == 2) type = "smoke";
        else type = "humidity";

        double val = readDoubleInRange("Enter value (0-100): ", 0, 100);

        addRawEvent(val, type, zone);
        addCorrectionEvent(val, type, zone);
        addCircularEvent(val, type, zone);
        cout << "Event added to all linked structures." << endl;
    }
    else if (choice == 2) traverseForward();
    else if (choice == 3) traverseBackward();
    else if (choice == 4) runCircularMonitoring();
    else if (choice == 5) restoreStableSnapshot();
}

void menuFireDetection()
{
    cout << "\n--- 4. Fire Detection and Control ---" << endl;
    cout << "4.1 Detect Fire Risk (Threshold Check)" << endl;
    cout << "4.2 Trigger Emergency Alert" << endl;
    cout << "4.3 Priority-Based Fire Response" << endl;
    cout << "4.4 Simulate Fire Spread (BFS)" << endl;
    cout << "4.5 Allocate Firefighting Resources" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        cout << "=== Fire Risk Detection ===" << endl;
        bool anyRisk = false;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            if (isAnomaly(i))
            {
                cout << "Zone " << i << ": FIRE RISK DETECTED (Temp=" << dynamicTemp[i]
                     << " Smoke=" << dynamicSmoke[i] << ")" << endl;
                anyRisk = true;
            }
        }
        if (!anyRisk)
        {
            cout << "No fire risk detected in any zone." << endl;
        }
    }
    else if (choice == 2)
    {
        int zone = readIntInRange("Enter zone to trigger emergency alert (0-9): ", 0, MAX_ZONES - 1);
        cout << "EMERGENCY ALERT triggered for Zone " << zone << "!" << endl;
        Task t;
        t.description = "Emergency alert Zone " + to_string(zone);
        t.priority = 3;
        t.zoneID = zone;
        enqueuePriority(emergencyQueue, t);
        sysMetrics.emergencyCount++;
        saveStableSnapshot();
    }
    else if (choice == 3)
    {
        cout << "Processing priority-based fire response tasks:" << endl;
        if (emergencyQueue.size == 0)
        {
            cout << "No emergency tasks in queue." << endl;
            return;
        }
        Task t = dequeue(emergencyQueue);
        cout << "Processing: " << t.description << " | Priority: " << t.priority << endl;
        sysMetrics.totalTasksProcessed++;
    }
    else if (choice == 4)
    {
        int zone = readIntInRange("Enter fire origin zone (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);
        bfsTraversal(zone);
    }
    else if (choice == 5)
    {
        cout << "=== Firefighting Resource Allocation ===" << endl;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            double risk = computeRiskScore(i);
            if (risk > 0.6)
            {
                cout << "Zone " << i << " [Risk=" << risk << "]: Deploy 2 units + aerial support" << endl;
            }
            else if (risk > 0.3)
            {
                cout << "Zone " << i << " [Risk=" << risk << "]: Deploy 1 monitoring unit" << endl;
            }
        }
    }
}

void menuTaskScheduling()
{
    cout << "\n--- 5. Task Scheduling System ---" << endl;
    cout << "5.1 Add Routine Task" << endl;
    cout << "5.2 Add Surveillance Task" << endl;
    cout << "5.3 Add Emergency Task (Priority Queue)" << endl;
    cout << "5.4 Process Tasks" << endl;
    cout << "5.5 Pause and Resume Tasks" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        Task t;
        cout << "Enter task description: ";
        getline(cin, t.description);
        if (t.description.empty())
        {
            cout << "Description cannot be empty." << endl;
            return;
        }
        t.zoneID = readIntInRange("Enter zone ID (0-9): ", 0, MAX_ZONES - 1);
        t.priority = 1;
        enqueue(routineQueue, t);
        cout << "Routine task added. Queue size: " << routineQueue.size << endl;
    }
    else if (choice == 2)
    {
        Task t;
        cout << "Enter task description: ";
        getline(cin, t.description);
        if (t.description.empty())
        {
            cout << "Description cannot be empty." << endl;
            return;
        }
        t.zoneID = readIntInRange("Enter zone ID (0-9): ", 0, MAX_ZONES - 1);
        t.priority = 2;
        enqueue(surveillanceQueue, t);
        cout << "Surveillance task added. Queue size: " << surveillanceQueue.size << endl;
    }
    else if (choice == 3)
    {
        Task t;
        cout << "Enter emergency task description: ";
        getline(cin, t.description);
        if (t.description.empty())
        {
            cout << "Description cannot be empty." << endl;
            return;
        }
        t.zoneID = readIntInRange("Enter zone ID (0-9): ", 0, MAX_ZONES - 1);
        t.priority = 3;
        enqueuePriority(emergencyQueue, t);
        sysMetrics.emergencyCount++;
        cout << "Emergency task added. Queue size: " << emergencyQueue.size << endl;
    }
    else if (choice == 4)
    {
        int queueChoice = readIntInRange("Which queue? (1=Routine 2=Surveillance 3=Emergency): ", 1, 3);

        if (queueChoice == 1)
        {
            Task t = dequeue(routineQueue);
            if (t.description != "EMPTY")
            {
                cout << "Processed: " << t.description << endl;
                sysMetrics.totalTasksProcessed++;
            }
        }
        else if (queueChoice == 2)
        {
            Task t = dequeue(surveillanceQueue);
            if (t.description != "EMPTY")
            {
                cout << "Processed: " << t.description << endl;
                sysMetrics.totalTasksProcessed++;
            }
        }
        else if (queueChoice == 3)
        {
            Task t = dequeue(emergencyQueue);
            if (t.description != "EMPTY")
            {
                cout << "Processed: " << t.description << endl;
                sysMetrics.totalTasksProcessed++;
            }
        }
    }
    else if (choice == 5)
    {
        cout << "Routine queue paused. " << routineQueue.size << " tasks on hold." << endl;
        cout << "Emergency queue continues: " << emergencyQueue.size << " tasks active." << endl;
        cout << "Routine queue resumed after emergency processing." << endl;
    }
}

void menuDecisionSystem()
{
    cout << "\n--- 6. Decision System ---" << endl;
    cout << "6.1 Compute Risk Score" << endl;
    cout << "6.2 Zone-Level Decision Tree" << endl;
    cout << "6.3 Regional Decision Processing" << endl;
    cout << "6.4 Global Emergency Decision" << endl;
    cout << "6.5 Execute Final Action" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        int zone = readIntInRange("Enter zone ID (0-9): ", 0, MAX_ZONES - 1);
        double risk = computeRiskScore(zone);
        cout << "Zone " << zone << " Risk Score: " << risk << endl;
        if (risk > 0.6)      cout << "Decision: EMERGENCY RESPONSE required." << endl;
        else if (risk > 0.3) cout << "Decision: INCREASED MONITORING." << endl;
        else                  cout << "Decision: NORMAL OPERATIONS." << endl;
    }
    else if (choice == 2)
    {
        TreeNode* tree = buildFireDecisionTree();
        cout << "=== Zone-Level Decision Tree ===" << endl;
        inorderDecision(tree, 0);
    }
    else if (choice == 3)
    {
        cout << "=== Regional Decision Processing ===" << endl;
        double regionRisk = 0;
        for (int i = 0; i < 5; i++)
        {
            regionRisk += computeRiskScore(i);
        }
        double avgRisk = regionRisk / 5.0;
        cout << "Average regional risk (Zones 0-4): " << avgRisk << endl;
        if (avgRisk > 0.5)
        {
            cout << "Regional escalation triggered." << endl;
        }
        else
        {
            cout << "Region is stable." << endl;
        }
    }
    else if (choice == 4)
    {
        cout << "=== Global Emergency Decision ===" << endl;
        double totalRisk = 0;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            totalRisk += computeRiskScore(i);
        }
        cout << "Total system risk: " << totalRisk << endl;
        if (totalRisk > 4.0)
        {
            cout << "GLOBAL EMERGENCY DECLARED. All zones on alert." << endl;
            sysMetrics.emergencyCount += MAX_ZONES;
        }
        else
        {
            cout << "System risk within manageable threshold." << endl;
        }
    }
    else if (choice == 5)
    {
        cout << "=== Executing Final Action ===" << endl;
        for (int i = 0; i < MAX_ZONES; i++)
        {
            double risk = computeRiskScore(i);
            if (risk > 0.6)
            {
                cout << "Zone " << i << ": EVACUATE & deploy full response team." << endl;
            }
            else if (risk > 0.3)
            {
                cout << "Zone " << i << ": ALERT nearby stations." << endl;
            }
            else
            {
                cout << "Zone " << i << ": Continue routine monitoring." << endl;
            }
        }
    }
}

void menuSpatialRouting()
{
    cout << "\n--- 7. Spatial Routing System ---" << endl;
    cout << "7.1 Load Graph (Adjacency List)" << endl;
    cout << "7.2 Load Graph (Adjacency Matrix)" << endl;
    cout << "7.3 BFS Traversal (Fire Spread)" << endl;
    cout << "7.4 DFS Traversal (Deep Analysis)" << endl;
    cout << "7.5 Compute Safe Path" << endl;
    cout << "7.6 Update Blocked Routes" << endl;

    int choice = readIntInRange("Enter choice (1-6): ", 1, 6);

    if (choice == 1)
    {
        loadDefaultGraph();
        cout << "Adjacency list representation:" << endl;
        for (int i = 0; i < numZones; i++)
        {
            cout << "Zone" << i << ": ";
            AdjNode* temp = adjList[i];
            while (temp != NULL)
            {
                cout << "-> Zone" << temp->dest << "(cost=" << temp->cost << ") ";
                temp = temp->next;
            }
            cout << endl;
        }
    }
    else if (choice == 2)
    {
        loadDefaultGraph();
        cout << "Adjacency matrix:" << endl;
        cout << "     ";
        for (int j = 0; j < numZones; j++) cout << "Z" << j << "   ";
        cout << endl;
        for (int i = 0; i < numZones; i++)
        {
            cout << "Z" << i << "  | ";
            for (int j = 0; j < numZones; j++)
            {
                cout << adjMatrix[i][j] << "   ";
            }
            cout << endl;
        }
    }
    else if (choice == 3)
    {
        int start = readIntInRange("Enter start zone (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);
        bfsTraversal(start);
    }
    else if (choice == 4)
    {
        int start = readIntInRange("Enter start zone (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);
        dfsTraversal(start);
    }
    else if (choice == 5)
    {
        int src  = readIntInRange("Enter source zone (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);
        int dest = readIntInRange("Enter destination zone (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);

        if (src == dest)
        {
            cout << "Source and destination are the same zone." << endl;
            return;
        }
        computeSafePath(src, dest);
    }
    else if (choice == 6)
    {
        int zone  = readIntInRange("Enter zone to update fire level (0-" + to_string(numZones - 1) + "): ", 0, numZones - 1);
        double level = readDoubleInRange("Enter fire level (0.0 - 1.0): ", 0.0, 1.0);
        fireLevel[zone] = level;
        cout << "Fire level for Zone " << zone << " updated to " << level << endl;
        cout << "Path costs through this zone will now be increased." << endl;
    }
}

void menuHashAccess()
{
    cout << "\n--- 8. Hash-Based Fast Access System ---" << endl;
    cout << "8.1 Insert Data (Hash Table)" << endl;
    cout << "8.2 Retrieve Data (O(1) Access)" << endl;
    cout << "8.3 Handle Collisions" << endl;
    cout << "8.4 Update Cache" << endl;
    cout << "8.5 View Index Table" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        int zone = readIntInRange("Enter zone ID to insert (0-9): ", 0, MAX_ZONES - 1);
        hashInsert(zone);
    }
    else if (choice == 2)
    {
        int zone = readIntInRange("Enter zone ID to retrieve (0-9): ", 0, MAX_ZONES - 1);
        hashRetrieve(zone);
    }
    else if (choice == 3)
    {
        cout << "=== Collision Demonstration ===" << endl;
        cout << "Inserting zones that may share same hash index..." << endl;
        hashInsert(0);
        hashInsert(17);
        hashInsert(34);
        cout << "Collision chain for index 0 created using chaining." << endl;
    }
    else if (choice == 4)
    {
        int zone = readIntInRange("Enter zone ID to cache (0-9): ", 0, MAX_ZONES - 1);
        updateCache(zone);
    }
    else if (choice == 5)
    {
        viewIndexTable();
    }
}

void menuSystemMonitoring()
{
    cout << "\n--- 9. System Monitoring ---" << endl;
    cout << "9.1 Monitor System Load" << endl;
    cout << "9.2 Track Execution Time" << endl;
    cout << "9.3 Detect Bottlenecks" << endl;
    cout << "9.4 Optimize Performance" << endl;
    cout << "9.5 View System Health" << endl;

    int choice = readIntInRange("Enter choice (1-5): ", 1, 5);

    if (choice == 1)
    {
        monitorSystemLoad();
    }
    else if (choice == 2)
    {
        clock_t start = clock();
        for (int i = 0; i < MAX_ZONES; i++)
        {
            computeRiskScore(i);
        }
        clock_t end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC * 1000.0;
        cout << "Risk scan across all zones completed in " << elapsed << " ms." << endl;
        cout << "Time complexity of risk scan: O(n) where n = number of zones." << endl;
    }
    else if (choice == 3)
    {
        detectBottlenecks();
    }
    else if (choice == 4)
    {
        cout << "=== Performance Optimization ===" << endl;
        cout << "Redistributing workload from routine queue..." << endl;
        int moved = 0;
        while (routineQueue.size > 5 && moved < 3)
        {
            Task t = dequeue(routineQueue);
            enqueue(multiQueue, t);
            moved++;
        }
        cout << moved << " tasks redistributed to multi-factor queue." << endl;
        cout << "Cache cleared and rebuilt for top-3 zones..." << endl;
        for (int i = 0; i < 3; i++) updateCache(i);
    }
    else if (choice == 5)
    {
        viewSystemHealth();
    }
}

void menuScenarios()
{
    cout << "\n--- 10. Scenario Simulation ---" << endl;
    cout << "10.1 Cascading Fire Scenario" << endl;
    cout << "10.2 Sensor Failure Scenario" << endl;
    cout << "10.3 Multi-Factor Anomaly Scenario" << endl;
    cout << "10.4 System Overload Scenario" << endl;
    cout << "10.5 Global Emergency Scenario" << endl;
    cout << "10.6 Run Full System Simulation" << endl;

    int choice = readIntInRange("Enter choice (1-6): ", 1, 6);

    if      (choice == 1) scenarioCascadingFire();
    else if (choice == 2) scenarioSensorFailure();
    else if (choice == 3) scenarioMultiFactorAnomaly();
    else if (choice == 4) scenarioSystemOverload();
    else if (choice == 5) scenarioGlobalEmergency();
    else if (choice == 6) runFullSimulation();
}

// ============================================================
// MAIN
// ============================================================

int main()
{
    initQueues();
    initHashTables();
    initGraph();
    loadDefaultGraph();
    saveStableSnapshot();

    cout << "============================================" << endl;
    cout << "  Intelligent Forest Advisory &" << endl;
    cout << "  Multi-Structure Decision System (IFAMDS)" << endl;
    cout << "  CL2001 - Data Structures 2026" << endl;
    cout << "============================================" << endl;

    int choice;

    do
    {
        cout << "\n========== MAIN MENU ==========" << endl;
        cout << "1.  Input Environmental Data" << endl;
        cout << "2.  View Forest Grid Status" << endl;
        cout << "3.  Event Memory System" << endl;
        cout << "4.  Fire Detection and Control" << endl;
        cout << "5.  Task Scheduling System" << endl;
        cout << "6.  Decision System" << endl;
        cout << "7.  Spatial Routing System" << endl;
        cout << "8.  Hash-Based Fast Access System" << endl;
        cout << "9.  System Monitoring" << endl;
        cout << "10. Scenario Simulation" << endl;
        cout << "0.  Exit System" << endl;
        cout << "================================" << endl;

        choice = readInt("Enter choice: ");

        if (choice < 0 || (choice > 10 && choice != 0))
        {
            cout << "Invalid option. Please enter a number between 0 and 10." << endl;
            continue;
        }

        if      (choice == 1)  menuInputEnvironmentalData();
        else if (choice == 2)  menuViewForestGrid();
        else if (choice == 3)  menuEventMemory();
        else if (choice == 4)  menuFireDetection();
        else if (choice == 5)  menuTaskScheduling();
        else if (choice == 6)  menuDecisionSystem();
        else if (choice == 7)  menuSpatialRouting();
        else if (choice == 8)  menuHashAccess();
        else if (choice == 9)  menuSystemMonitoring();
        else if (choice == 10) menuScenarios();
        else if (choice == 0)  cout << "Exiting IFAMDS. Goodbye." << endl;

    } while (choice != 0);

    return 0;
}