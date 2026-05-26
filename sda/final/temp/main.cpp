// IntelliFly.cpp
// IntelliFly: Advanced Flight Scheduler System (Full Educational Version with Interactive CLI)
// Compile: g++ -std=c++17 IntelliFly.cpp -O2 -o IntelliFly
// Run: ./IntelliFly

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
using ll = long long;

// ----------------- Utilities -----------------

static long long parseTimeHM(const string &s) {
    int hh = 0, mm = 0;
    if (sscanf(s.c_str(), "%d:%d", &hh, &mm) != 2) return -1;
    return hh * 3600 + mm * 60;
}

static string fmtHM(long long seconds) {
    if (seconds < 0) return "N/A";
    int hh = (int)((seconds / 3600) % 24);
    int mm = (int)((seconds % 3600) / 60);
    char buf[16];
    sprintf(buf, "%02d:%02d", hh, mm);
    return string(buf);
}

static string fmtArrival(long long dep, long long arr) {
    string s = fmtHM(arr);
    if (arr < dep) s += " (+1d)";
    return s;
}

static string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Safe input helpers (paste once near top of the file)
static string readLinePrompt(const string &prompt = "") {
    if (!prompt.empty()) {
        cout << prompt;
        cout.flush();
    }
    string line;
    if (!getline(cin, line)) return string(); // handles EOF
    return line;
}

static int readIntPrompt(const string &prompt = "") {
    while (true) {
        string line = readLinePrompt(prompt);
        if (line.empty()) {
            cout << "Please enter a number.\n";
            continue;
        }
        try {
            size_t pos;
            int v = stoi(line, &pos);
            if (pos != line.size()) {
                cout << "Invalid number (extra characters). Try again.\n";
                continue;
            }
            return v;
        } catch (...) {
            cout << "Invalid input. Enter a number.\n";
        }
    }
}

// ----------------- Models -----------------

struct Flight {
    string flightNo;
    string origin, dest;
    long long departure;
    long long arrival;
    int priority; // 1 emergency, 2 VIP, 3 normal
    bool valid = true;
    string description;

    Flight() = default;

    Flight(string f, string o, string d, long long dep, long long arr, int p, string desc = "")
        : flightNo(move(f)), origin(move(o)), dest(move(d)),
          departure(dep), arrival(arr), priority(p), valid(true),
          description(move(desc)) {}
};



struct Runway {
    int id;
    bool free = true;
    Runway(int _id = 0) : id(_id), free(true) {}
};

// ----------------- Sorting Algorithms -----------------

namespace sorting {

template<typename T, typename Comp>
void mergeSortRec(vector<T> &a, int l, int r, vector<T> &buf, Comp comp) {
    if (l >= r) return;
    int m = (l + r) / 2;
    mergeSortRec(a, l, m, buf, comp);
    mergeSortRec(a, m + 1, r, buf, comp);

    int i = l, j = m + 1, k = l;
    while (i <= m || j <= r) {
        if (i <= m && (j > r || comp(a[i], a[j]))) buf[k++] = a[i++];
        else buf[k++] = a[j++];
    }
    for (int t = l; t <= r; ++t) a[t] = buf[t];
}

template<typename T, typename Comp>
void mergeSort(vector<T> &a, Comp comp) {
    if (a.empty()) return;
    vector<T> buf(a.size());
    mergeSortRec(a, 0, (int)a.size() - 1, buf, comp);
}

template<typename T, typename Comp>
int partition(vector<T> &a, int l, int r, Comp comp) {
    int m = (l + r) / 2;
    if (comp(a[m], a[l])) swap(a[l], a[m]);
    if (comp(a[r], a[l])) swap(a[l], a[r]);
    if (comp(a[r], a[m])) swap(a[m], a[r]);

    T pivot = a[m];
    swap(a[m], a[r - 1]);

    int i = l, j = r - 1;
    while (true) {
        while (comp(a[++i], pivot));
        while (comp(pivot, a[--j]));
        if (i < j) swap(a[i], a[j]);
        else break;
    }
    swap(a[i], a[r - 1]);
    return i;
}

template<typename T, typename Comp>
void quickSortRec(vector<T> &a, int l, int r, Comp comp) {
    if (r - l <= 10) {
        for (int i = l + 1; i <= r; ++i) {
            T key = a[i];
            int j = i - 1;
            while (j >= l && comp(key, a[j])) {
                a[j + 1] = a[j];
                --j;
            }
            a[j + 1] = key;
        }
        return;
    }
    int p = partition(a, l, r, comp);
    quickSortRec(a, l, p - 1, comp);
    quickSortRec(a, p + 1, r, comp);
}

template<typename T, typename Comp>
void quickSort(vector<T> &a, Comp comp) {
    if (a.empty()) return;
    quickSortRec(a, 0, (int)a.size() - 1, comp);
}

} // namespace sorting


template<typename Key, typename Val>
class AVL {
    struct Node {
        Key key;
        Val val;
        Node *l = nullptr, *r = nullptr;
        int height = 1;
        Node(const Key &k, const Val &v) : key(k), val(v), l(nullptr), r(nullptr), height(1) {}
    };
    Node* root = nullptr;

    int height(Node* n) { return n ? n->height : 0; }
    int balance(Node* n) { return n ? height(n->l) - height(n->r) : 0; }
    void update(Node* n) { if (n) n->height = 1 + max(height(n->l), height(n->r)); }

    Node* rotateRight(Node* y) {
        Node* x = y->l;
        Node* T2 = x->r;
        x->r = y;
        y->l = T2;
        update(y);
        update(x);
        return x;
    }

    Node* rotateLeft(Node* x) {
        Node* y = x->r;
        Node* T2 = y->l;
        y->l = x;
        x->r = T2;
        update(x);
        update(y);
        return y;
    }

    Node* rebalance(Node* n) {
        update(n);
        int b = balance(n);
        if (b > 1) {
            if (balance(n->l) < 0) n->l = rotateLeft(n->l);
            return rotateRight(n);
        }
        if (b < -1) {
            if (balance(n->r) > 0) n->r = rotateRight(n->r);
            return rotateLeft(n);
        }
        return n;
    }

    Node* insert(Node* node, const Key &k, const Val &v) {
        if (!node) return new Node(k, v);
        if (k < node->key) node->l = insert(node->l, k, v);
        else if (node->key < k) node->r = insert(node->r, k, v);
        else node->val = v;
        return rebalance(node);
    }

    Node* findMin(Node* node) { while (node->l) node = node->l; return node; }

    Node* remove(Node* node, const Key &k) {
        if (!node) return nullptr;
        if (k < node->key) node->l = remove(node->l, k);
        else if (node->key < k) node->r = remove(node->r, k);
        else {
            if (!node->l) { Node* r = node->r; delete node; return r; }
            if (!node->r) { Node* l = node->l; delete node; return l; }
            Node* mn = findMin(node->r);
            node->key = mn->key; node->val = mn->val;
            node->r = remove(node->r, mn->key);
        }
        return rebalance(node);
    }

    Val* search(Node* node, const Key &k) {
        if (!node) return nullptr;
        if (k < node->key) return search(node->l, k);
        else if (node->key < k) return search(node->r, k);
        else return &node->val;
    }

    void inorder(Node* node, vector<pair<Key, Val>> &out) {
        if (!node) return;
        inorder(node->l, out);
        out.push_back({node->key, node->val});
        inorder(node->r, out);
    }

public:
    void insert(const Key &k, const Val &v) { root = insert(root, k, v); }
    void remove(const Key &k) { root = remove(root, k); }
    Val* search(const Key &k) { return search(root, k); }
    vector<pair<Key, Val>> inorder() { vector<pair<Key, Val>> out; inorder(root, out); return out; }
    ~AVL() { clear(root); }

private:
    void clear(Node* n) { if (!n) return; clear(n->l); clear(n->r); delete n; }
};

// ----------------- B-Tree -----------------

template<typename Key, typename Val>
class BTree {
    struct Node {
        bool leaf;
        vector<Key> keys;
        vector<Val> vals;
        vector<Node*> children;
        Node(bool leaf = false) : leaf(leaf) {}
    };

    Node* root = nullptr;
    int t;

    Val* search(Node* x, const Key &k) {
        if (!x) return nullptr;
        int i = 0;
        while (i < (int)x->keys.size() && k > x->keys[i]) ++i;
        if (i < (int)x->keys.size() && !(x->keys[i] < k) && !(k < x->keys[i])) return &x->vals[i];
        if (x->leaf) return nullptr;
        return search(x->children[i], k);
    }

    void splitChild(Node* x, int i) {
        Node* y = x->children[i];
        Node* z = new Node(y->leaf);
        int mid = t - 1;
        for (int j = 0; j < mid; ++j) { z->keys.push_back(y->keys[j + t]); z->vals.push_back(y->vals[j + t]); }
        if (!y->leaf) { for (int j = 0; j < t; ++j) z->children.push_back(y->children[j + t]); }

        y->keys.resize(t - 1); y->vals.resize(t - 1);
        if (!y->leaf) y->children.resize(t);

        x->children.insert(x->children.begin() + i + 1, z);
        x->keys.insert(x->keys.begin() + i, y->keys[mid]);
        x->vals.insert(x->vals.begin() + i, y->vals[mid]);
    }

    void insertNonFull(Node* x, const Key &k, const Val &v) {
        int i = (int)x->keys.size() - 1;
        if (x->leaf) {
            x->keys.push_back(k);
            x->vals.push_back(v);
            while (i >= 0 && x->keys[i] > x->keys[i + 1]) {
                swap(x->keys[i], x->keys[i + 1]);
                swap(x->vals[i], x->vals[i + 1]);
                --i;
            }
        } else {
            while (i >= 0 && k < x->keys[i]) --i;
            ++i;
            if ((int)x->children[i]->keys.size() == 2 * t - 1) {
                splitChild(x, i);
                if (k > x->keys[i]) ++i;
            }
            insertNonFull(x->children[i], k, v);
        }
    }

public:
    BTree(int t = 2) : t(t) { root = new Node(true); }
    ~BTree() { destroy(root); }

    Val* search(const Key &k) { return search(root, k); }

    void insert(const Key &k, const Val &v) {
        if ((int)root->keys.size() == 2 * t - 1) {
            Node* s = new Node(false);
            s->children.push_back(root);
            root = s;
            splitChild(s, 0);
            insertNonFull(s, k, v);
        } else insertNonFull(root, k, v);
    }

private:
    void destroy(Node* x) { if (!x) return; for (auto* c : x->children) destroy(c); delete x; }
};

// ----------------- Priority Heaps -----------------

template<typename T, typename Key>
class IndexedMinHeap {
    vector<T> heap;
    unordered_map<Key, int> indexMap;
    function<Key(const T&)> keyOf;
    function<bool(const T&, const T&)> lessThan;

    void swapAt(int i, int j) {
        auto ki = keyOf(heap[i]);
        auto kj = keyOf(heap[j]);
        swap(heap[i], heap[j]);
        indexMap[ki] = j;
        indexMap[kj] = i;
    }

    void siftUp(int i) {
        while (i > 0) {
            int p = (i - 1) / 2;
            if (lessThan(heap[i], heap[p])) { swapAt(i, p); i = p; }
            else break;
        }
    }

    void siftDown(int i) {
        int n = (int)heap.size();
        while (true) {
            int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
            if (l < n && lessThan(heap[l], heap[smallest])) smallest = l;
            if (r < n && lessThan(heap[r], heap[smallest])) smallest = r;
            if (smallest != i) { swapAt(i, smallest); i = smallest; }
            else break;
        }
    }

public:
    IndexedMinHeap(function<Key(const T&)> keyFunc, function<bool(const T&, const T&)> cmp)
        : keyOf(keyFunc), lessThan(cmp) {}

    bool empty() const { return heap.empty(); }
    int size() const { return (int)heap.size(); }

    void push(const T &val) {
        Key k = keyOf(val);
        heap.push_back(val);
        indexMap[k] = (int)heap.size() - 1;
        siftUp((int)heap.size() - 1);
    }

    T top() const { return heap.front(); }

    void pop() {
        if (heap.empty()) return;
        Key k = keyOf(heap.front());
        indexMap.erase(k);
        swapAt(0, (int)heap.size() - 1);
        heap.pop_back();
        if (!heap.empty()) siftDown(0);
    }

    bool contains(const Key &k) const { return indexMap.find(k) != indexMap.end(); }

    void decreaseKey(const Key &k, const T &newVal) {
        auto it = indexMap.find(k);
        if (it == indexMap.end()) return;
        int pos = it->second;
        heap[pos] = newVal;
        indexMap[keyOf(newVal)] = pos;
        siftUp(pos);
    }
};

// ----------------- Hash Tables -----------------

template<typename K, typename V>
class ChainedHashTable {
    vector<list<pair<K, V>>> buckets;
    size_t n;
    double maxLoad = 0.7;
    hash<K> hasher;

    void rehash() {
        size_t newcap = buckets.size() * 2;
        vector<list<pair<K, V>>> nb(newcap);
        for (auto &b : buckets) for (auto &p : b) nb[hasher(p.first) % newcap].push_back(p);
        buckets.swap(nb);
    }

public:
    ChainedHashTable(size_t cap = 16) : buckets(cap), n(0) {}

    void insert(const K &k, const V &v) {
        if (((double)n + 1) / buckets.size() > maxLoad) rehash();
        size_t idx = hasher(k) % buckets.size();
        for (auto &p : buckets[idx]) if (p.first == k) { p.second = v; return; }
        buckets[idx].push_back({k, v}); ++n;
    }

    bool remove(const K &k) {
        size_t idx = hasher(k) % buckets.size();
        for (auto it = buckets[idx].begin(); it != buckets[idx].end(); ++it)
            if (it->first == k) { buckets[idx].erase(it); --n; return true; }
        return false;
    }

    V* find(const K &k) {
        size_t idx = hasher(k) % buckets.size();
        for (auto &p : buckets[idx]) if (p.first == k) return &p.second;
        return nullptr;
    }
};

template<typename K, typename V>
class OpenAddressingHash {
    enum State { EMPTY, OCCUPIED, DELETED };
    struct Slot { K key; V val; State st = EMPTY; };
    vector<Slot> table;
    size_t n = 0;
    double maxLoad = 0.5;
    hash<K> hasher;

    void rehash() {
        vector<Slot> old = table;
        table.assign(table.size() * 2, Slot());
        n = 0;
        for (auto &s : old) if (s.st == OCCUPIED) insert(s.key, s.val);
    }

public:
    OpenAddressingHash(size_t cap = 16) { table.assign(cap, Slot()); }

    void insert(const K &k, const V &v) {
        if (((double)n + 1) / table.size() > maxLoad) rehash();
        size_t idx = hasher(k) % table.size();
        while (table[idx].st == OCCUPIED) {
            if (table[idx].st == OCCUPIED && table[idx].key == k) { table[idx].val = v; return; }
            idx = (idx + 1) % table.size();
        }
        table[idx].key = k; table[idx].val = v; table[idx].st = OCCUPIED; ++n;
    }

    bool remove(const K &k) {
        size_t idx = hasher(k) % table.size();
        size_t start = idx;
        while (table[idx].st != EMPTY) {
            if (table[idx].st == OCCUPIED && table[idx].key == k) { table[idx].st = DELETED; --n; return true; }
            idx = (idx + 1) % table.size();
            if (idx == start) break;
        }
        return false;
    }

    V* find(const K &k) {
        size_t idx = hasher(k) % table.size();
        size_t start = idx;
        while (table[idx].st != EMPTY) {
            if (table[idx].st == OCCUPIED && table[idx].key == k) return &table[idx].val;
            idx = (idx + 1) % table.size();
            if (idx == start) break;
        }
        return nullptr;
    }
};

// ----------------- Trie -----------------

class Trie {
    struct Node { bool end = false; unordered_map<char, Node*> nxt; };
    Node* root;

    void collect(Node* node, string &acc, vector<string> &out, int limit) {
        if ((int)out.size() >= limit) return;
        if (node->end) out.push_back(acc);
        for (auto &p : node->nxt) {
            acc.push_back(p.first);
            collect(p.second, acc, out, limit);
            acc.pop_back();
            if ((int)out.size() >= limit) return;
        }
    }

    void destroy(Node* n) { if (!n) return; for (auto &p : n->nxt) destroy(p.second); delete n; }

public:
    Trie() { root = new Node(); }
    ~Trie() { destroy(root); }

    void insert(const string &s) {
        Node* cur = root;
        for (char c : s) {
            if (!cur->nxt[c]) cur->nxt[c] = new Node();
            cur = cur->nxt[c];
        }
        cur->end = true;
    }

    vector<string> autocomplete(const string &pref, int limit = 10) {
        vector<string> out;
        Node* cur = root;
        for (char c : pref) {
            if (!cur->nxt.count(c)) return out;
            cur = cur->nxt[c];
        }
        string acc = pref;
        collect(cur, acc, out, limit);
        return out;
    }
};

// ----------------- String Search -----------------

namespace strsearch {

vector<int> brute(const string &t, const string &p) {
    vector<int> out; int n = (int)t.size(), m = (int)p.size();
    for (int i = 0; i + m <= n; ++i) {
        int j = 0; while (j < m && t[i + j] == p[j]) ++j;
        if (j == m) out.push_back(i);
    }
    return out;
}

vector<int> kmp(const string &t, const string &p) {
    int n = (int)t.size(), m = (int)p.size();
    vector<int> out;
    if (m == 0) return out;
    vector<int> lps(m);
    for (int i = 1, len = 0; i < m;) {
        if (p[i] == p[len]) lps[i++] = ++len;
        else if (len) len = lps[len - 1];
        else lps[i++] = 0;
    }
    for (int i = 0, j = 0; i < n;) {
        if (t[i] == p[j]) {
            ++i; ++j;
            if (j == m) { out.push_back(i - j); j = lps[j - 1]; }
        } else if (j) j = lps[j - 1];
        else ++i;
    }
    return out;
}

vector<int> rabinKarp(const string &t, const string &p) {
    vector<int> out; int n = (int)t.size(), m = (int)p.size();
    if (m == 0 || m > n) return out;
    const unsigned long long base = 1315423911ULL;
    unsigned long long ph = 0, th = 0, powB = 1;
    for (int i = 0; i < m; ++i) {
        ph = ph * base + (unsigned char)p[i];
        th = th * base + (unsigned char)t[i];
        if (i) powB *= base;
    }
    for (int i = 0; i + m <= n; ++i) {
        if (i > 0) {
            th = th - (unsigned long long)(unsigned char)t[i - 1] * powB;
            th = th * base + (unsigned char)t[i + m - 1];
        }
        if (th == ph) { if (t.compare(i, m, p) == 0) out.push_back(i); }
    }
    return out;
}

vector<int> boyerMoore(const string &t, const string &p) {
    vector<int> out; int n = (int)t.size(), m = (int)p.size();
    if (m == 0) return out;
    unordered_map<char, int> bad;
    for (int i = 0; i < m; ++i) bad[p[i]] = i;
    int i = 0;
    while (i <= n - m) {
        int j = m - 1;
        while (j >= 0 && t[i + j] == p[j]) --j;
        if (j < 0) {
            out.push_back(i);
            i += (i + m < n ? m - bad[t[i + m]] : 1);
        } else {
            auto it = bad.find(t[i + j]);
            int shift = (it == bad.end() ? j + 1 : max(1, j - it->second));
            i += shift;
        }
    }
    return out;
}

} // namespace strsearch

// ----------------- Graph -----------------

struct Edge { string u, v; int w; Edge() {} Edge(string a, string b, int c) : u(move(a)), v(move(b)), w(c) {} };

class Graph {
    unordered_map<string, vector<pair<string, int>>> adj;
    vector<Edge> edgesList;

public:
    void addEdge(const string &u, const string &v, int w) {
        adj[u].push_back({v, w});
        edgesList.emplace_back(u, v, w);
    }

    vector<string> bfs(const string &src) {
        vector<string> visited; unordered_set<string> seen; queue<string> q;
        if (!adj.count(src)) return visited;
        q.push(src); seen.insert(src);
        while (!q.empty()) {
            string u = q.front(); q.pop(); visited.push_back(u);
            for (auto &p : adj[u]) if (!seen.count(p.first)) { seen.insert(p.first); q.push(p.first); }
        }
        return visited;
    }

    void dfsRec(const string &u, unordered_set<string> &seen, vector<string> &out) {
        seen.insert(u); out.push_back(u);
        for (auto &p : adj[u]) if (!seen.count(p.first)) dfsRec(p.first, seen, out);
    }

    vector<string> dfs(const string &src) {
        vector<string> out; unordered_set<string> seen;
        if (!adj.count(src)) return out;
        dfsRec(src, seen, out); return out;
    }

    unordered_map<string, int> dijkstra(const string &src) {
        const int INF = 1e9;
        unordered_map<string, int> dist;
        for (auto &p : adj) dist[p.first] = INF;
        if (!adj.count(src)) return dist;
        dist[src] = 0;
        using P = pair<int, string>;
        priority_queue<P, vector<P>, greater<P>> pq; pq.push({0, src});
        while (!pq.empty()) {
            auto [d, u] = pq.top(); pq.pop();
            if (d != dist[u]) continue;
            for (auto &e : adj[u]) if (dist[e.first] > d + e.second) {
                dist[e.first] = d + e.second;
                pq.push({dist[e.first], e.first});
            }
        }
        return dist;
    }

    vector<Edge> primMST(const string &start) {
        vector<Edge> mst;
        if (!adj.count(start)) return mst;
        unordered_set<string> inMST;
        using T = tuple<int, string, string>;
        priority_queue<T, vector<T>, greater<T>> pq;
        inMST.insert(start);
        for (auto &e : adj[start]) pq.push({e.second, start, e.first});
        while (!pq.empty()) {
            auto [w, u, v] = pq.top(); pq.pop();
            if (inMST.count(v)) continue;
            mst.emplace_back(u, v, w);
            inMST.insert(v);
            for (auto &e : adj[v]) if (!inMST.count(e.first)) pq.push({e.second, v, e.first});
        }
        return mst;
    }

    vector<Edge> kruskalMST() {
        vector<Edge> all = edgesList;
        unordered_map<string, int> idx; int id = 0;
        for (auto &p : adj) {
            if (!idx.count(p.first)) idx[p.first] = id++;
            for (auto &e : p.second) if (!idx.count(e.first)) idx[e.first] = id++;
        }
        int n = id;
        vector<int> parent(n); iota(parent.begin(), parent.end(), 0);
        function<int(int)> findp = [&](int x) { return parent[x] == x ? x : parent[x] = findp(parent[x]); };
        sort(all.begin(), all.end(), [](const Edge &a, const Edge &b) { return a.w < b.w; });
        vector<Edge> mst;
        for (auto &e : all) {
            int u = idx[e.u], v = idx[e.v];
            int pu = findp(u), pv = findp(v);
            if (pu != pv) { parent[pu] = pv; mst.push_back(e); }
        }
        return mst;
    }

    vector<string> topoSort() {
        unordered_map<string, int> indeg;
        for (auto &p : adj) {
            if (!indeg.count(p.first)) indeg[p.first] = 0;
            for (auto &e : p.second) indeg[e.first]++;
        }
        queue<string> q;
        for (auto &p : indeg) if (p.second == 0) q.push(p.first);
        vector<string> order;
        while (!q.empty()) {
            string u = q.front(); q.pop(); order.push_back(u);
            for (auto &e : adj[u]) if (--indeg[e.first] == 0) q.push(e.first);
        }
        return order;
    }

    vector<string> vertices() { vector<string> out; for (auto &p : adj) out.push_back(p.first); return out; }
};

// ----------------- Flight Manager -----------------

class FlightManager {
    vector<Flight> flights;
    ChainedHashTable<string, int> flightIndex;
    OpenAddressingHash<string, int> flightIndexOA;
    Trie airportTrie;
    Graph routeGraph;
    AVL<long long, string> scheduleAVL;
    BTree<string, int> btreeIndex;

    struct HeapItem { string flightNo; int priority; long long time; };

    function<string(const HeapItem&)> heapKey = [](const HeapItem &h) -> string { return h.flightNo; };
    function<bool(const HeapItem&, const HeapItem&)> heapLess = [](const HeapItem &a, const HeapItem &b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.time < b.time;
    };

    IndexedMinHeap<HeapItem, string> indexedHeap;
    priority_queue<HeapItem, vector<HeapItem>, function<bool(const HeapItem&, const HeapItem&)>> lazyPQ;
    vector<Runway> runways;
    stack<string> undoStack;

public:
    FlightManager()
        : flightIndex(128), flightIndexOA(128), btreeIndex(2),
          indexedHeap(heapKey, heapLess),
          lazyPQ([&](const HeapItem &a, const HeapItem &b) { return heapLess(b, a); })
    {
        vector<string> sample = {"LHE", "KHI", "JFK", "LAX", "DXB", "ATL", "ORD", "LHR", "AMS", "PEK", "ISB", "MUX"};
        for (auto &s : sample) airportTrie.insert(s);
        for (int i = 1; i <= 3; ++i) runways.emplace_back(i);
    }

    void addRoute(const string &a, const string &b, int w) {
        routeGraph.addEdge(a, b, w); routeGraph.addEdge(b, a, w);
    }

    bool addFlight(const Flight &f) {
        int *existing = flightIndex.find(f.flightNo);
        if (existing) return false;
        int idx = (int)flights.size();
        flights.push_back(f);
        flightIndex.insert(f.flightNo, idx);
        flightIndexOA.insert(f.flightNo, idx);
        airportTrie.insert(f.origin); airportTrie.insert(f.dest);
        scheduleAVL.insert(f.departure, f.flightNo);
        btreeIndex.insert(f.flightNo, idx);
        HeapItem h{f.flightNo, f.priority, f.departure};
        indexedHeap.push(h);
        lazyPQ.push(h);
        undoStack.push("ADD " + f.flightNo);
        return true;
    }

    bool cancelFlight(const string &flightNo) {
        int *pi = flightIndex.find(flightNo);
        if (!pi) return false;
        int idx = *pi;
        flights[idx].valid = false;
        flightIndex.remove(flightNo);
        flightIndexOA.remove(flightNo);
        scheduleAVL.remove(flights[idx].departure);
        undoStack.push("CANCEL " + flightNo);
        return true;
    }

    optional<Flight> getFlight(const string &flightNo) {
        int *pi = flightIndex.find(flightNo);
        if (!pi) return {};
        int idx = *pi;
        if (flights[idx].valid) return flights[idx];
        return {};
    }

    vector<Flight> getAllFlights() {
        vector<Flight> result;
        for (auto &f : flights) if (f.valid) result.push_back(f);
        return result;
    }

    optional<Flight> scheduleNext() {
        while (!lazyPQ.empty()) {
            HeapItem top = lazyPQ.top(); lazyPQ.pop();
            int *pi = flightIndexOA.find(top.flightNo);
            if (pi) {
                int idx = *pi;
                if (idx >= 0 && idx < (int)flights.size() && flights[idx].valid) return flights[idx];
            }
        }
        return {};
    }

    optional<int> allocateRunway(const string &flightNo) {
        int *pi = flightIndex.find(flightNo);
        if (!pi) return {};
        int idx = *pi;
        if (idx < 0 || idx >= (int)flights.size() || !flights[idx].valid) return {};
        for (auto &r : runways) {
            if (r.free) {
                r.free = false;
                undoStack.push("ALLOC " + to_string(r.id) + " " + flightNo);
                return r.id;
            }
        }
        return {};
    }

    bool freeRunway(int id) {
        for (auto &r : runways) {
            if (r.id == id) {
                r.free = true;
                undoStack.push("FREE " + to_string(id));
                return true;
            }
        }
        return false;
    }

    vector<pair<string, int>> shortestFrom(const string &src) {
        unordered_map<string, int> d = routeGraph.dijkstra(src);
        vector<pair<string, int>> out;
        for (auto &p : d) if (p.second < 1e8) out.push_back(p);
        return out;
    }

    vector<Edge> getMSTbyKruskal() { return routeGraph.kruskalMST(); }
    vector<Edge> getMSTbyPrim(const string &start) { return routeGraph.primMST(start); }
    vector<string> topoOrder() { return routeGraph.topoSort(); }
    vector<string> autocompleteAirport(const string &pref, int limit = 10) { return airportTrie.autocomplete(pref, limit); }

    vector<pair<string, string>> listFlightsSortedByDeparture(bool stable = true) {
        vector<pair<long long, string>> arr;
        for (auto &f : flights) if (f.valid) arr.push_back({f.departure, f.flightNo});
        if (stable) sorting::mergeSort(arr, [](auto &a, auto &b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        else sorting::quickSort(arr, [](auto &a, auto &b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

        vector<pair<string, string>> out;
        for (auto &p : arr) out.push_back({p.second, fmtHM(p.first)});
        return out;
    }

    vector<Flight> getFlightsSortedByDeparture() {
        vector<Flight> result = getAllFlights();
        sorting::mergeSort(result, [](const Flight &a, const Flight &b) { return a.departure < b.departure; });
        return result;
    }

    vector<Flight> getFlightsByPriority() {
        vector<Flight> result = getAllFlights();
        sorting::quickSort(result, [](const Flight &a, const Flight &b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.departure < b.departure;
        });
        return result;
    }

    vector<int> searchInLogs(const string &pattern, const string &method = "kmp") {
        string big;
        for (auto &f : flights) if (f.valid) big += f.flightNo + ":" + f.origin + "->" + f.dest + " " + f.description + "\n";
        if (method == "kmp") return strsearch::kmp(big, pattern);
        if (method == "rk") return strsearch::rabinKarp(big, pattern);
        if (method == "bm") return strsearch::boyerMoore(big, pattern);
        return strsearch::brute(big, pattern);
    }

    vector<Runway> getRunwayStatus() { return runways; }

    void printAllFlights() {
        cout << "All flights:\n";
        for (auto &f : flights) if (f.valid) cout << f.flightNo << " " << f.origin << "->" << f.dest
            << " dep=" << fmtHM(f.departure) << " pri=" << f.priority << "\n";
    }

    bool undo() {
        if (undoStack.empty()) return false;
        string act = undoStack.top(); undoStack.pop();

        auto parts = vector<string>();
        {
            string token; stringstream ss(act);
            while (ss >> token) parts.push_back(token);
        }

        if (parts.empty()) return true;
        if (parts[0] == "ADD") {
            int *pi = flightIndex.find(parts[1]);
            if (pi) {
                int idx = *pi;
                flights[idx].valid = false;
                flightIndex.remove(parts[1]);
                flightIndexOA.remove(parts[1]);
                scheduleAVL.remove(flights[idx].departure);
                cout << "Undo: removed flight " << parts[1] << "\n";
            }
            return true;
        } else if (parts[0] == "ALLOC") {
            int id = stoi(parts[1]);
            freeRunway(id);
            cout << "Undo: freed runway " << id << "\n";
            return true;
        } else if (parts[0] == "CANCEL") {
            cout << "Undo: CANCEL action is not reversible in current design.\n";
            return true;
        } else if (parts[0] == "FREE") {
            cout << "Undo: FREE runway action not reversible (missing flight association).\n";
            return true;
        }
        return false;
    }
};

// ----------------- CLI Interface -----------------

class CLI {
    FlightManager fm;

    void printHeader() {
        cout << "\n================================================\n";
        cout << "       IntelliFly Flight Scheduler v1.0        \n";
        cout << "================================================\n\n";
    }

    void printMenu() {
        cout << "\n================ MAIN MENU ================\n";
        cout << "  1. Add Flight\n";
        cout << "  2. Cancel Flight\n";
        cout << "  3. View Flight Details\n";
        cout << "  4. List All Flights\n";
        cout << "  5. List Flights by Departure Time\n";
        cout << "  6. List Flights by Priority\n";
        cout << "  7. Manage Runways\n";
        cout << "  8. Route Planning\n";
        cout << "  9. Search in Descriptions\n";
        cout << " 10. Airport Autocomplete\n";
        cout << " 11. Run Demo Scenario\n";
        cout << "  0. Exit\n";
        cout << "============================================\n";
    }

    void addFlightMenu() {
        cout << "\n=== Add New Flight ===\n";
        string flightNo, origin, dest, depTime, arrTime, desc;
        int priority;

        flightNo = readLinePrompt("Flight Number: ");
        flightNo = trim(flightNo);
        if (flightNo.empty()) { cout << "Invalid flight number!\n"; return; }

        origin = readLinePrompt("Origin (e.g., LHE): ");
        origin = trim(origin);
        transform(origin.begin(), origin.end(), origin.begin(), ::toupper);

        dest = readLinePrompt("Destination (e.g., KHI): ");
        dest = trim(dest);
        transform(dest.begin(), dest.end(), dest.begin(), ::toupper);

        depTime = readLinePrompt("Departure Time (HH:MM): ");
        long long dep = parseTimeHM(trim(depTime));
        if (dep < 0) { cout << "Invalid time format!\n"; return; }

        arrTime = readLinePrompt("Arrival Time (HH:MM): ");
        long long arr = parseTimeHM(trim(arrTime));
        if (arr < 0) { cout << "Invalid time format!\n"; return; }

        priority = readIntPrompt("Priority (1=Emergency, 2=VIP, 3=Normal): ");
        if (priority < 1 || priority > 3) { cout << "Invalid priority!\n"; return; }

        desc = readLinePrompt("Description (optional): ");

        Flight f(flightNo, origin, dest, dep, arr, priority, trim(desc));
        if (fm.addFlight(f)) {
            cout << "[SUCCESS] Flight " << flightNo << " added successfully!\n";
        } else {
            cout << "[ERROR] Flight already exists!\n";
        }
    }

    void cancelFlightMenu() {
        cout << "\n=== Cancel Flight ===\n";
        string flightNo;
        flightNo = readLinePrompt("Flight Number: ");
        flightNo = trim(flightNo);

        if (fm.cancelFlight(flightNo)) {
            cout << "[SUCCESS] Flight " << flightNo << " cancelled successfully!\n";
        } else {
            cout << "[ERROR] Flight not found!\n";
        }
    }

    void viewFlightMenu() {
        cout << "\n=== View Flight Details ===\n";
        string flightNo;
        flightNo = readLinePrompt("Flight Number: ");
        flightNo = trim(flightNo);

        auto flight = fm.getFlight(flightNo);
        if (flight) {
            cout << "\n+-------------------------------------+\n";
            cout << "| Flight: " << setw(28) << left << flight->flightNo << "|\n";
            cout << "+-------------------------------------+\n";
            cout << "| Route: " << flight->origin << " -> " << flight->dest << string(24 - flight->origin.size() - flight->dest.size(), ' ') << "|\n";
            cout << "| Departure: " << setw(25) << fmtHM(flight->departure) << "|\n";
            cout << "| Arrival: " << setw(27) << fmtArrival(flight->departure, flight->arrival) << "|\n";
            string pri = flight->priority == 1 ? "Emergency" : flight->priority == 2 ? "VIP" : "Normal";
            cout << "| Priority: " << pri << string(26 - pri.size(), ' ') << "|\n";
            if (!flight->description.empty())
                cout << "| Info: " << setw(30) << left << flight->description.substr(0, 30) << "|\n";
            cout << "+-------------------------------------+\n";
        } else {
            cout << "[ERROR] Flight not found!\n";
        }
    }

    void listAllFlightsMenu() {
        cout << "\n=== All Flights ===\n";
        auto flights = fm.getAllFlights();
        if (flights.empty()) { cout << "No flights available.\n"; return; }

        cout << left << setw(10) << "Flight" << setw(12) << "Route"
             << setw(12) << "Departure" << setw(10) << "Priority" << "\n";
        cout << string(54, '-') << "\n";

        for (auto &f : flights) {
            string route = f.origin + "->" + f.dest;
            string pri = f.priority == 1 ? "Emergency" : f.priority == 2 ? "VIP" : "Normal";
            cout << left << setw(10) << f.flightNo << setw(12) << route
                 << setw(12) << fmtHM(f.departure) << setw(10) << pri << "\n";
        }
    }

    void listByDepartureMenu() {
        cout << "\n=== Flights Sorted by Departure ===\n";
        auto flights = fm.getFlightsSortedByDeparture();
        if (flights.empty()) { cout << "No flights available.\n"; return; }

        cout << left << setw(10) << "Flight" << setw(12) << "Route"
             << setw(12) << "Departure" << setw(16) << "Arrival" << "\n";
        cout << string(54, '-') << "\n";

        for (auto &f : flights) {
            string route = f.origin + "->" + f.dest;
            cout << left << setw(10) << f.flightNo << setw(12) << route
                 << setw(12) << fmtHM(f.departure) << setw(16) << fmtArrival(f.departure, f.arrival) << "\n";
        }
    }

    void listByPriorityMenu() {
        cout << "\n=== Flights Sorted by Priority ===\n";
        auto flights = fm.getFlightsByPriority();
        if (flights.empty()) { cout << "No flights available.\n"; return; }

        cout << left << setw(10) << "Flight" << setw(12) << "Route"
             << setw(12) << "Departure" << setw(12) << "Priority" << "\n";
        cout << string(56, '-') << "\n";

        for (auto &f : flights) {
            string route = f.origin + "->" + f.dest;
            string pri = f.priority == 1 ? "Emergency" : f.priority == 2 ? "VIP" : "Normal";
            cout << left << setw(10) << f.flightNo << setw(12) << route
                 << setw(12) << fmtHM(f.departure) << setw(12) << pri << "\n";
        }
    }

    void runwayMenu() {
        cout << "\n═══ Runway Management ═══\n";
        cout << "1. View Runway Status\n";
        cout << "2. Allocate Runway to Flight\n";
        cout << "3. Free Runway\n";
        int choice = readIntPrompt("Select: ");

        if (choice == 1) {
            auto runways = fm.getRunwayStatus();
            cout << "\n" << left << setw(12) << "Runway" << setw(10) << "Status" << "\n";
            cout << string(22, '-') << "\n";
            for (auto &r : runways) {
                cout << left << setw(12) << ("Runway " + to_string(r.id))
                     << setw(10) << (r.free ? "Free" : "Occupied") << "\n";
            }
        } else if (choice == 2) {
            string flightNo;
            flightNo = readLinePrompt("Flight Number: ");
            auto runway = fm.allocateRunway(trim(flightNo));
            if (runway) {
                cout << "✓ Runway " << *runway << " allocated to " << flightNo << "\n";
            } else {
                cout << "✗ No runway available!\n";
            }
        } else if (choice == 3) {
            int id;
            id = readIntPrompt("Runway ID: ");
            if (fm.freeRunway(id)) {
                cout << "✓ Runway " << id << " freed!\n";
            } else {
                cout << "✗ Invalid runway ID!\n";
            }
        }
    }

    void routePlanningMenu() {
        cout << "\n═══ Route Planning ═══\n";
        cout << "1. Find Shortest Distances from Airport\n";
        cout << "2. View Minimum Spanning Tree (MST)\n";
        int choice = readIntPrompt("Select: ");

        if (choice == 1) {
            string airport;
            airport = readLinePrompt("Airport Code: ");
            airport = trim(airport);
            transform(airport.begin(), airport.end(), airport.begin(), ::toupper);

            auto distances = fm.shortestFrom(airport);
            if (distances.empty()) { cout << "No routes found!\n"; return; }

            cout << "\nShortest distances from " << airport << ":\n";
            cout << left << setw(15) << "Destination" << setw(15) << "Distance (min)" << "\n";
            cout << string(30, '-') << "\n";
            for (auto &[dest, dist] : distances) {
                cout << left << setw(15) << dest << setw(15) << dist << "\n";
            }
        } else if (choice == 2) {
            auto mst = fm.getMSTbyKruskal();
            if (mst.empty()) { cout << "No MST available!\n"; return; }

            cout << "\nMinimum Spanning Tree:\n";
            cout << left << setw(12) << "From" << setw(12) << "To" << setw(12) << "Weight" << "\n";
            cout << string(36, '-') << "\n";
            int total = 0;
            for (auto &e : mst) {
                cout << left << setw(12) << e.u << setw(12) << e.v << setw(12) << e.w << "\n";
                total += e.w;
            }
            cout << "Total Weight: " << total << " minutes\n";
        }
    }

    void searchMenu() {
        cout << "\n═══ Search in Descriptions ═══\n";
        string pattern;
        pattern = readLinePrompt("Search pattern: ");
        pattern = trim(pattern);

        auto positions = fm.searchInLogs(pattern);
        if (positions.empty()) {
            cout << "No matches found.\n";
        } else {
            cout << "Found " << positions.size() << " matches at positions: ";
            for (size_t i = 0; i < min(positions.size(), size_t(10)); i++) {
                cout << positions[i] << " ";
            }
            if (positions.size() > 10) cout << "...";
            cout << "\n";
        }
    }

    void autocompleteMenu() {
        cout << "\n═══ Airport Autocomplete ═══\n";
        string prefix;
        prefix = readLinePrompt("Enter prefix: ");
        prefix = trim(prefix);
        transform(prefix.begin(), prefix.end(), prefix.begin(), ::toupper);

        auto suggestions = fm.autocompleteAirport(prefix);
        if (suggestions.empty()) {
            cout << "No suggestions found.\n";
        } else {
            cout << "Suggestions: ";
            for (auto &s : suggestions) cout << s << " ";
            cout << "\n";
        }
    }

    void demoScenario() {
        cout << "\n═══ Running Demo Scenario ═══\n";
        fm.addRoute("LHE", "KHI", 60);
        fm.addRoute("LHE", "DXB", 180);
        fm.addRoute("KHI", "DXB", 150);
        fm.addRoute("DXB", "LAX", 900);
        fm.addRoute("LAX", "JFK", 300);
        fm.addRoute("JFK", "LHR", 420);

        cout << "\n-- Flight list --\n"; fm.printAllFlights();

        cout << "\n-- Schedule next (priority) --\n";
        auto next = fm.scheduleNext();
        if (next) cout << "Next scheduled: " << next->flightNo << " dep=" << fmtHM(next->departure) << "\n";
        else cout << "No scheduled flight\n";

        cout << "\n-- Shortest distances from LHE (Dijkstra) --\n";
        auto d = fm.shortestFrom("LHE");
        for (auto &p : d) cout << p.first << " -> " << p.second << " minutes\n";

        cout << "\n-- Kruskal MST edges --\n";
        auto mst = fm.getMSTbyKruskal();
        for (auto &e : mst) cout << e.u << " - " << e.v << " w=" << e.w << "\n";

        cout << "\n-- Autocomplete airports for 'L' --\n";
        auto ac = fm.autocompleteAirport("L");
        for (auto &s : ac) cout << s << " ";
        cout << "\n";

        cout << "\n-- Flights sorted by departure (merge sort) --\n";
        auto sorted = fm.listFlightsSortedByDeparture(true);
        for (auto &p : sorted) cout << p.first << " at " << p.second << "\n";

        cout << "\n-- Demonstrate Runway allocation --\n";
        auto allocate = fm.allocateRunway("FX100");
        if (allocate) cout << "Allocated runway " << *allocate << " to FX100\n";
        else cout << "No runway available\n";
    }

    void loadSampleData() {
        fm.addRoute("LHE", "KHI", 60);
        fm.addRoute("LHE", "DXB", 180);
        fm.addRoute("KHI", "DXB", 150);
        fm.addRoute("DXB", "LAX", 900);
        fm.addRoute("LAX", "JFK", 300);

        fm.addFlight(Flight("PK301", "LHE", "KHI", parseTimeHM("08:00"), parseTimeHM("09:00"), 3, "Daily morning flight"));
        fm.addFlight(Flight("EM999", "KHI", "DXB", parseTimeHM("06:30"), parseTimeHM("09:00"), 1, "Emergency medical"));
        fm.addFlight(Flight("VIP200", "LHE", "DXB", parseTimeHM("14:00"), parseTimeHM("17:00"), 2, "VIP charter"));
    }

public:
    void run() {
        printHeader();
        cout << "Loading sample data..." << endl;
        loadSampleData();
        cout << "Ready!" << endl;

        while (true) {
            printMenu();
            int choice = readIntPrompt("Select option: ");
            if (choice == 0) {
                cout << "\nThank you for using IntelliFly! Safe travels!\n\n";
                break;
            }

            switch (choice) {
                case 1: addFlightMenu(); break;
                case 2: cancelFlightMenu(); break;
                case 3: viewFlightMenu(); break;
                case 4: listAllFlightsMenu(); break;
                case 5: listByDepartureMenu(); break;
                case 6: listByPriorityMenu(); break;
                case 7: runwayMenu(); break;
                case 8: routePlanningMenu(); break;
                case 9: searchMenu(); break;
                case 10: autocompleteMenu(); break;
                case 11: demoScenario(); break;
                default: cout << "Invalid option! Please try again.\n";
            }

            cout << "\nPress Enter to continue...";
            readLinePrompt("");
        }
    }
};

// ----------------- Main -----------------

int main() {
    
    CLI cli;
    cli.run();

    return 0;
}
