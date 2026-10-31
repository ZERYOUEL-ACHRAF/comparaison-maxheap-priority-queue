// ============================================================================
//  Comparaison de conteneurs "file de priorite max"
//  - MaxHeap maison (tas binaire cree from scratch)
//  - std::priority_queue
//
//  Phases mesurees :
//    Insertion            : N insertions
//    Max seul             : lecture de la racine (le tas reste inchange)
//    Extraction pure      : racine <- dernier element, size-- (sans reorganisation)
//    Regeneration         : reorganisation du tas (sift-down)
//    Extraction complete  : Extraction pure + Regeneration
//
//  Menu 1 : MaxHeap maison / priority_queue / COMPARER LES DEUX
//  Menu 2 : 1) N aleatoires  2) N manuels  3) benchmark (mediane de 5 runs)
//
//  Affichage : un seul tableau compact, temps en ns par operation.
//              Une ligne par conteneur et par taille N.
// ============================================================================
#include <iostream>
#include <chrono>
#include <random>
#include <stdexcept>
#include <iomanip>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <queue>
#include <numeric>
#include <limits>
#include <memory>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

const int MAX_N = 10000000;

// ---------------------------------------------------------------------------
// Chronometre haute precision (en nanosecondes)
// Sous Windows, std::chrono::steady_clock est tres grossier avec certains
// MinGW (resolution ~1 ms), ce qui donnait des "0.0 ns" : on utilise donc
// QueryPerformanceCounter.
// ---------------------------------------------------------------------------
#ifdef _WIN32
inline double nowNs() {
    static double freq = 0;
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e9 / freq;
}
#else
inline double nowNs() {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

// ---------------------------------------------------------------------------
// Tas max cree from scratch (tableau dynamique + sift-up / sift-down)
// ---------------------------------------------------------------------------
class MaxHeap {
private:
    int* arr;
    int size;
    int capacity;

public:
    MaxHeap() : arr(nullptr), size(0), capacity(0) {}
    ~MaxHeap() { delete[] arr; }

    MaxHeap(const MaxHeap&) = delete;
    MaxHeap& operator=(const MaxHeap&) = delete;

    void reserve(int cap) {
        delete[] arr;
        arr = new int[cap];
        capacity = cap;
        size = 0;
    }

    void insert(int value) {
        if (size >= capacity) throw std::overflow_error("Capacite maximale atteinte.");

        int i = size++;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (arr[parent] >= value) break;
            arr[i] = arr[parent];
            i = parent;
        }
        arr[i] = value;
    }

    // Lecture du max seul (O(1)), le tas reste inchange
    int peek() const {
        if (size <= 0) throw std::out_of_range("Le tas est vide.");
        return arr[0];
    }

    int extractMax() {
        if (size <= 0) throw std::out_of_range("Le tas est vide.");

        int maxVal  = arr[0];
        int lastVal = arr[--size];
        if (size == 0) return maxVal;

        int i = 0;
        while (2 * i + 1 < size) {
            int left = 2 * i + 1, right = left + 1, big = left;
            if (right < size && arr[right] > arr[left]) big = right;
            if (lastVal >= arr[big]) break;
            arr[i] = arr[big];
            i = big;
        }
        arr[i] = lastVal;
        return maxVal;
    }

    // Partie O(1) seule de l'extraction (lecture de la racine, deplacement du
    // dernier element a la racine, size--), SANS ajustement. Le tas devient
    // invalide : uniquement utilise pour mesurer le cout de cette partie.
    int extractRootOnly() {
        if (size <= 0) throw std::out_of_range("Le tas est vide.");
        int maxVal = arr[0];
        arr[0] = arr[--size];
        return maxVal;
    }
};

// ---------------------------------------------------------------------------
// Interface commune : prepare / insert / peek / extractMax / extractRootOnly
// ---------------------------------------------------------------------------
struct HeapC {
    MaxHeap h;
    void prepare(int n) { h.reserve(n); }
    void insert(int v) { h.insert(v); }
    int peek() { return h.peek(); }
    int extractMax() { return h.extractMax(); }
    int extractRootOnly() { return h.extractRootOnly(); }
};

struct PQueueC {
    // On derive de priority_queue pour acceder au vecteur interne protege 'c'
    struct PQ : std::priority_queue<int> {
        void prepare(int n) { c.clear(); c.reserve(n); }
        int extractRootOnly() {             // meme operation que le tas maison
            int m = c.front();
            c.front() = c.back();
            c.pop_back();
            return m;
        }
    };
    PQ q;
    void prepare(int n) { q.prepare(n); }
    void insert(int v) { q.push(v); }
    int peek() { return q.top(); }
    int extractMax() { int m = q.top(); q.pop(); return m; }
    int extractRootOnly() { return q.extractRootOnly(); }
};

// ---------------------------------------------------------------------------
// Les 5 phases (indices dans Phases::t)
// ---------------------------------------------------------------------------
const int NPH = 5;
const char* const PHASE_NAME[NPH] = {"Insertion", "Max seul", "Extraction pure",
                                     "Regeneration", "Extraction complete"};
const char* const PHASE_CX[NPH]   = {"O(log N)", "O(1)", "O(1)", "O(log N)", "O(log N)"};
// 0 = Insertion, 1 = Max seul, 2 = Extraction pure, 3 = Regeneration,
// 4 = Extraction complete (= 2 + 3)

struct Phases {
    double t[NPH];   // temps TOTAL en ns pour N elements
    bool ok;         // sortie correcte (ordre decroissant + meme somme)
};

// ---------------------------------------------------------------------------
// Mesure d'un conteneur.
// Pour les petits N, la mesure est faite sur plusieurs tas a la suite (inner)
// afin que la duree chronometree soit assez longue ; le resultat est ramene
// a UNE execution de N elements.
// Aucun appel a l'horloge a l'interieur des boucles.
// ---------------------------------------------------------------------------
template <class C>
Phases measurePhases(const std::vector<int>& data) {
    const int N = (int)data.size();
    const int inner = std::max(1, std::min(1000000 / N, 1000));
    const long long expected = std::accumulate(data.begin(), data.end(), 0LL);
    volatile long long sink = 0;

    Phases d;
    d.ok = true;
    double tIns, tMax, tTotal, tPure;

    // Passe 1 : insertion, max seul, extraction complete (pure + regeneration)
    {
        std::unique_ptr<C[]> cs(new C[inner]);
        for (int k = 0; k < inner; k++) cs[k].prepare(N);     // allocation hors chrono

        double t0 = nowNs();
        for (int k = 0; k < inner; k++)
            for (int i = 0; i < N; i++) cs[k].insert(data[i]);
        double t1 = nowNs();

        for (int k = 0; k < inner; k++)
            for (int i = 0; i < N; i++) sink = sink + cs[k].peek();   // tas inchange
        double t2 = nowNs();

        for (int k = 0; k < inner; k++) {
            long long checksum = 0;
            int prev = std::numeric_limits<int>::max();
            for (int i = 0; i < N; i++) {
                int v = cs[k].extractMax();
                if (v > prev) d.ok = false;
                prev = v;
                checksum += v;
            }
            if (checksum != expected) d.ok = false;
        }
        double t3 = nowNs();

        tIns   = t1 - t0;
        tMax   = t2 - t1;
        tTotal = t3 - t2;
    }

    // Passe 2 : extraction pure seule (sans reorganisation)
    {
        std::unique_ptr<C[]> ps(new C[inner]);
        for (int k = 0; k < inner; k++) {
            ps[k].prepare(N);
            for (int i = 0; i < N; i++) ps[k].insert(data[i]);
        }
        double t4 = nowNs();
        for (int k = 0; k < inner; k++)
            for (int i = 0; i < N; i++) sink = sink + ps[k].extractRootOnly();
        double t5 = nowNs();
        tPure = t5 - t4;
    }

    d.t[0] = tIns / inner;
    d.t[1] = tMax / inner;
    d.t[4] = tTotal / inner;
    d.t[2] = std::min(tPure / inner, d.t[4]);
    d.t[3] = d.t[4] - d.t[2];
    return d;
}

struct Entry {
    std::string name;
    Phases (*run)(const std::vector<int>&);
};

const std::vector<Entry>& registry() {
    static const std::vector<Entry> r = {
        {"MaxHeap maison", &measurePhases<HeapC>},
        {"priority_queue", &measurePhases<PQueueC>},
    };
    return r;
}

// ---------------------------------------------------------------------------
// Utilitaires d'affichage
// ---------------------------------------------------------------------------
std::string fmtTime(double ns) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ns;
    return oss.str();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n % 2 == 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

typedef std::vector<std::vector<std::string> > ResultRows;

// Une ligne par conteneur : toutes les phases utilisent la meme unite.
void addResultRow(ResultRows& rows, int n, const std::string& name, const Phases& r) {
    std::vector<std::string> row = {std::to_string(n), name};
    for (int p = 0; p < NPH; p++) row.push_back(fmtTime(r.t[p] / n));
    row.push_back(r.ok ? "OK" : "ERREUR");
    rows.push_back(row);
}

void printResults(const std::string& title, const ResultRows& rows, size_t groupEvery) {
    const std::vector<std::string> head = {
        "N", "Conteneur", "Insertion", "Max", "Retrait brut", "Reorg.*", "Extraction", "Controle"
    };
    std::vector<size_t> widths(head.size());
    for (size_t i = 0; i < head.size(); i++) widths[i] = head[i].size();
    for (size_t k = 0; k < rows.size(); k++)
        for (size_t i = 0; i < head.size(); i++)
            widths[i] = std::max(widths[i], rows[k][i].size());

    size_t width = 1;
    for (size_t i = 0; i < widths.size(); i++) width += widths[i] + 3;
    const std::string line(width, '-');
    auto printRow = [&](const std::vector<std::string>& row) {
        std::cout << "|";
        for (size_t i = 0; i < row.size(); i++)
            std::cout << " " << (i == 1 ? std::left : std::right)
                      << std::setw((int)widths[i]) << row[i] << " |";
        std::cout << "\n";
    };

    std::cout << "\n " << title << " | ns/operation (plus petit = plus rapide)\n";
    std::cout << line << "\n";
    printRow(head);
    std::cout << line << "\n";
    for (size_t k = 0; k < rows.size(); k++) {
        printRow(rows[k]);
        if (groupEvery > 0 && (k + 1) % groupEvery == 0 && k + 1 < rows.size())
            std::cout << line << "\n";
    }
    std::cout << line << "\n";
    std::cout << " Retrait brut : sans reorganisation. * Reorg. = extraction - retrait brut (estimation).\n";
}

std::vector<int> generateRandom(int N, std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(1, 1000000000);
    std::vector<int> data(N);
    for (int i = 0; i < N; i++) data[i] = dist(rng);
    return data;
}

// ---------------------------------------------------------------------------
// Choix 1 / 2 : une execution, un tableau de resultats.
// ---------------------------------------------------------------------------
void printSingle(const Entry& e, const std::vector<int>& data) {
    ResultRows rows;
    addResultRow(rows, (int)data.size(), e.name, e.run(data));
    printResults("RESULTATS", rows, 1);
}

void printDuo(const std::vector<int>& data) {
    const std::vector<Entry>& reg = registry();
    ResultRows rows;
    for (size_t c = 0; c < reg.size(); c++)
        addResultRow(rows, (int)data.size(), reg[c].name, reg[c].run(data));
    printResults("COMPARAISON", rows, reg.size());
}

// ---------------------------------------------------------------------------
// Benchmark : toutes les tailles et tous les conteneurs dans un seul tableau.
// scope >= 0 : un seul conteneur ; scope < 0 : les deux.
// ---------------------------------------------------------------------------
void runBenchmark(int scope) {
    const int REPS = 5;
    const std::vector<int> sizes = {1000, 10000, 100000, 1000000, 10000000};
    const std::vector<Entry>& reg = registry();
    std::vector<int> ids;
    if (scope < 0) { ids.push_back(0); ids.push_back(1); }
    else           ids.push_back(scope);

    const size_t C = ids.size();
    std::mt19937 rng(std::random_device{}());
    ResultRows rows;

    std::cout << "\nBenchmark : " << REPS << " executions par taille..." << std::flush;
    for (size_t s = 0; s < sizes.size(); s++) {
        const int N = sizes[s];
        std::vector<std::vector<std::vector<double> > > acc(C, std::vector<std::vector<double> >(NPH));
        std::vector<bool> valid(C, true);
        for (int rep = 0; rep < REPS; rep++) {
            std::vector<int> data = generateRandom(N, rng);   // memes donnees pour les deux
            for (size_t c = 0; c < C; c++) {
                Phases r = reg[ids[c]].run(data);
                if (!r.ok) valid[c] = false;
                for (int p = 0; p < NPH; p++) acc[c][p].push_back(r.t[p]);
            }
        }
        for (size_t c = 0; c < C; c++) {
            Phases result;
            for (int p = 0; p < NPH; p++) result.t[p] = median(acc[c][p]);
            result.ok = valid[c];
            addResultRow(rows, N, reg[ids[c]].name, result);
        }
        std::cout << "." << std::flush;
    }
    std::cout << " termine.\n";
    printResults("BENCHMARK | mediane de " + std::to_string(REPS) + " executions", rows, C);
}

// ---------------------------------------------------------------------------
// Saisie securisee
// ---------------------------------------------------------------------------
bool readInt(const std::string& prompt, int& out) {
    std::cout << prompt << std::flush;
    if (!(std::cin >> out)) {
        if (std::cin.eof()) return false;
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        out = -1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Menu des 3 modes pour la portee choisie
// sel = 0 : MaxHeap maison, 1 : priority_queue, -1 : les deux
// Retourne false si l'entree est fermee (fin du programme)
// ---------------------------------------------------------------------------
bool modeMenu(int sel) {
    const std::vector<Entry>& reg = registry();
    const std::string scopeName = (sel < 0) ? "MAXHEAP vs PRIORITY_QUEUE" : reg[sel].name;

    while (true) {
        std::cout << "\n--- " << scopeName << " ---\n";
        std::cout << "1. Inserer N elements aleatoires\n";
        std::cout << "2. Inserer N elements manuellement\n";
        std::cout << "3. Benchmark automatique (1000 -> 10^7), mediane de 5 executions\n";
        std::cout << "0. Retour\n";

        int mode;
        if (!readInt("Choix : ", mode)) return false;
        if (mode == 0) return true;

        if (mode == 1 || mode == 2) {
            int N;
            if (!readInt("Combien d'elements (1 - " + std::to_string(MAX_N) + ") ? : ", N)) return false;
            if (N <= 0 || N > MAX_N) { std::cout << "N invalide.\n"; continue; }

            std::vector<int> data;
            if (mode == 1) {
                std::mt19937 rng(std::random_device{}());
                data = generateRandom(N, rng);
            } else {
                std::cout << "Saisissez les " << N << " elements :\n";
                data.resize(N);
                bool good = true;
                for (int i = 0; i < N; i++) {
                    if (!(std::cin >> data[i])) {
                        if (std::cin.eof()) return false;
                        std::cin.clear();
                        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        good = false;
                        break;
                    }
                }
                if (!good) { std::cout << "Saisie invalide.\n"; continue; }
            }

            if (sel < 0) printDuo(data);
            else         printSingle(reg[sel], data);
        }
        else if (mode == 3) {
            runBenchmark(sel);
        }
        else {
            std::cout << "Choix invalide.\n";
        }
    }
}

int main() {
    while (true) {
        std::cout << "\n=====================================================\n";
        std::cout << "  Test de performance : file de priorite (max)\n";
        std::cout << "=====================================================\n";
        std::cout << "Conteneur a tester :\n";
        std::cout << "  1. MaxHeap maison (tas cree from scratch)\n";
        std::cout << "  2. std::priority_queue\n";
        std::cout << "  3. COMPARER les deux\n";
        std::cout << "  0. Quitter\n";

        int scope;
        if (!readInt("Choix : ", scope)) break;
        if (scope == 0) break;
        if (scope < 1 || scope > 3) { std::cout << "Choix invalide.\n"; continue; }

        int sel = (scope == 3) ? -1 : scope - 1;
        if (!modeMenu(sel)) break;
    }

    std::cout << "Au revoir.\n";
    return 0;
}