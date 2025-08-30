// CPU Scheduling Algorithm Simulator and Evaluator
// Language: C++17
// -------------------------------------------------------------
// Brief report (design & notes):
// - Implements FCFS, SJF (non-preemptive), Priority (non-preemptive), and Round Robin (preemptive).
// - Menu-driven CLI with robust input validation and error handling.
// - Generates an ASCII Gantt chart (with IDLE periods) and prints per-process
//   metrics (Waiting/Turnaround/Completion) and averages.
// - Comparison module runs all algorithms on the same process set (RR asks
//   for Quantum) and selects the one with the smallest average waiting time.
// - Data structures: Process to hold inputs; Segment to record timeline; Result
//   to capture schedule, metrics, and averages. Queues/vectors used as needed.
// - Assumptions: lower priority value means higher priority. SJF & Priority are
//   non-preemptive; Round Robin is preemptive. Arrival times are supported.
// - Compile:  g++ -std=gnu++17 -O2 -pipe -static -s -o scheduler cpu_scheduler.cpp
// - Run:      ./scheduler
// -------------------------------------------------------------

#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <string>
#include <sstream>
#include <cmath>

using namespace std;

struct Process {
    int pid;       // 1..N
    int arrival;   // >= 0
    int burst;     // > 0
    int priority;  // smaller = higher priority (used in Priority scheduling)
};

struct Segment {
    int pid;   // -1 for IDLE, else PID
    int start; // inclusive
    int end;   // exclusive
};

struct Result {
    vector<Segment> timeline;            // scheduling timeline
    vector<int> completion, waiting, tat;// per-pid metrics (indexed by pid, 1..N)
    double avg_wait = 0.0;
    double avg_tat = 0.0;
    string algo_name;
};

// ---------- Input utilities ----------
static void clearInput() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

static int readInt(const string &prompt, long long lo, long long hi) {
    while (true) {
        cout << prompt;
        long long x;
        if (cin >> x) {
            if (x < lo || x > hi) {
                cout << "\n[Error] Enter a value in [" << lo << ", " << hi << "].\n\n";
                continue;
            }
            clearInput();
            return (int)x;
        } else {
            cout << "\n[Error] Invalid number. Try again.\n\n";
            clearInput();
        }
    }
}

static bool readYesNo(const string &prompt, bool defaultYes = true) {
    while (true) {
        cout << prompt << (defaultYes ? " [Y/n]: " : " [y/N]: ");
        string s; getline(cin, s);
        if (s.empty()) return defaultYes;
        for (char &c : s) c = (char)tolower(c);
        if (s == "y" || s == "yes") return true;
        if (s == "n" || s == "no") return false;
        cout << "Please answer y/yes or n/no.\n";
    }
}

// ---------- Pretty printing ----------

static void printProcessTable(const vector<Process>& ps) {
    cout << "\nProcesses (lower priority value = higher priority):\n";
    cout << left << setw(6) << "PID" << setw(10) << "Arrival" << setw(8) << "Burst" << setw(9) << "Priority" << "\n";
    cout << string(36, '-') << "\n";
    for (const auto &p : ps) {
        cout << left << setw(6) << p.pid << setw(10) << p.arrival << setw(8) << p.burst << setw(9) << p.priority << "\n";
    }
}

static void drawGantt(const vector<Segment>& segs) {
    if (segs.empty()) { cout << "\n[Gantt] (no segments)\n"; return; }

    int total = segs.back().end;
    double scale = (total > 80) ? (double)total / 80.0 : 1.0; // compress long timelines

    // Build two rows: a bar and a label row
    string bar, labels;
    for (const auto &s : segs) {
        int duration = max(0, s.end - s.start);
        int w = max(1, (int)round(duration / scale));
        bar += "|";
        bar += string(w, '-');

        labels += "|";
        string lab = (s.pid == -1) ? "IDLE" : ("P" + to_string(s.pid));
        if (w >= (int)lab.size()) {
            int left = (w - (int)lab.size()) / 2;
            int right = w - (int)lab.size() - left;
            labels += string(left, ' ') + lab + string(right, ' ');
        } else {
            labels += string(w, ' ');
        }
    }
    bar += "|"; labels += "|";

    cout << "\nGantt Chart (scaled):\n";
    cout << bar << "\n";
    cout << labels << "\n";

    // Time ruler
    cout << "0";
    int printed = 0;
    for (const auto &s : segs) {
        int duration = max(0, s.end - s.start);
        int w = max(1, (int)round(duration / scale));
        string t = to_string(s.end);
        int spaces = w + 1 - (int)t.size(); // +1 for the vertical bar
        if (spaces < 1) spaces = 1;
        cout << string(spaces, ' ') << t;
        printed += spaces + (int)t.size();
    }
    cout << "\n";
}

static void printResult(const Result &res, const vector<Process>& ps) {
    cout << "\n=== " << res.algo_name << " Result ===\n";
    drawGantt(res.timeline);

    cout << "\nPer-Process Metrics:\n";
    cout << left << setw(6) << "PID" << setw(10) << "Arrival" << setw(8) << "Burst" 
         << setw(11) << "Complete" << setw(12) << "Turnaround" << setw(9) << "Waiting" << "\n";
    cout << string(56, '-') << "\n";

    // For stable lookup by PID
    vector<Process> byPid(ps.size()+1);
    for (auto &p : ps) byPid[p.pid] = p;

    for (size_t pid = 1; pid < res.completion.size(); ++pid) {
        if (byPid[pid].pid == 0) continue; // skip if not present
        cout << left << setw(6) << pid
             << setw(10) << byPid[pid].arrival
             << setw(8)  << byPid[pid].burst
             << setw(11) << res.completion[pid]
             << setw(12) << res.tat[pid]
             << setw(9)  << res.waiting[pid] << "\n";
    }

    cout << fixed << setprecision(2);
    cout << "\nAverage Waiting Time   : " << res.avg_wait << "\n";
    cout << "Average Turnaround Time: " << res.avg_tat << "\n\n";
}

// ---------- Metrics ----------
static Result finalizeMetrics(const string& name, const vector<Process>& ps, const vector<Segment>& tl) {
    int n = (int)ps.size();
    Result r; r.algo_name = name; r.timeline = tl;
    r.completion.assign(n+1, 0);
    r.waiting.assign(n+1, 0);
    r.tat.assign(n+1, 0);

    // Map by PID for quick access
    vector<Process> byPid(n+1);
    for (auto &p : ps) byPid[p.pid] = p;

    // Completion time = last end occurrence in timeline for that PID
    for (const auto &s : tl) {
        if (s.pid == -1) continue;
        r.completion[s.pid] = max(r.completion[s.pid], s.end);
    }

    double sumWait = 0.0, sumTat = 0.0;
    for (int pid = 1; pid <= n; ++pid) {
        const auto &p = byPid[pid];
        int comp = r.completion[pid];
        int tat = comp - p.arrival;
        int wait = tat - p.burst;
        if (tat < 0) tat = 0; // safety
        if (wait < 0) wait = 0; // safety for malformed inputs
        r.tat[pid] = tat;
        r.waiting[pid] = wait;
        sumWait += wait; sumTat += tat;
    }

    if (n > 0) {
        r.avg_wait = sumWait / n;
        r.avg_tat = sumTat / n;
    }
    return r;
}

// ---------- Algorithms ----------

static Result runFCFS(const vector<Process>& ps) {
    vector<Process> a = ps;
    sort(a.begin(), a.end(), [](const Process& x, const Process& y){
        if (x.arrival != y.arrival) return x.arrival < y.arrival;
        return x.pid < y.pid;
    });

    vector<Segment> tl;
    int t = 0;
    for (const auto &p : a) {
        if (t < p.arrival) { // idle gap
            tl.push_back({-1, t, p.arrival});
            t = p.arrival;
        }
        tl.push_back({p.pid, t, t + p.burst});
        t += p.burst;
    }
    return finalizeMetrics("FCFS", ps, tl);
}

static Result runSJF(const vector<Process>& ps) {
    int n = (int)ps.size();
    vector<Process> a = ps;

    // Remaining set of unscheduled processes
    vector<bool> done(n+1, false);
    int finished = 0;
    int t = 0;
    vector<Segment> tl;

    // Pre-sort by arrival, then burst, then pid for stable tie-breaking
    vector<Process> sorted = ps;
    sort(sorted.begin(), sorted.end(), [](const Process& x, const Process& y){
        if (x.arrival != y.arrival) return x.arrival < y.arrival;
        if (x.burst   != y.burst)   return x.burst   < y.burst;
        return x.pid < y.pid;
    });

    while (finished < n) {
        // Collect ready
        vector<Process> ready;
        for (const auto &p : sorted)
            if (!done[p.pid] && p.arrival <= t) ready.push_back(p);

        if (ready.empty()) {
            // jump to next arrival
            int next_arr = INT_MAX; const Process* nxt = nullptr;
            for (const auto &p : sorted) if (!done[p.pid]) {
                if (p.arrival < next_arr) { next_arr = p.arrival; nxt = &p; }
            }
            if (nxt && t < nxt->arrival) {
                tl.push_back({-1, t, nxt->arrival});
                t = nxt->arrival;
            }
            continue; // re-evaluate ready set
        }

        // Choose shortest job among ready
        auto best = min_element(ready.begin(), ready.end(), [](const Process& x, const Process& y){
            if (x.burst != y.burst) return x.burst < y.burst;
            if (x.arrival != y.arrival) return x.arrival < y.arrival;
            return x.pid < y.pid;
        });
        tl.push_back({best->pid, t, t + best->burst});
        t += best->burst;
        done[best->pid] = true;
        finished++;
    }
    return finalizeMetrics("SJF (Non-Preemptive)", ps, tl);
}

static Result runPriorityNP(const vector<Process>& ps) {
    int n = (int)ps.size();
    vector<bool> done(n+1, false);
    int finished = 0; int t = 0; vector<Segment> tl;

    vector<Process> sorted = ps;
    sort(sorted.begin(), sorted.end(), [](const Process& x, const Process& y){
        if (x.arrival != y.arrival) return x.arrival < y.arrival;
        if (x.priority != y.priority) return x.priority < y.priority; // smaller is higher
        return x.pid < y.pid;
    });

    while (finished < n) {
        vector<Process> ready;
        for (const auto &p : sorted)
            if (!done[p.pid] && p.arrival <= t) ready.push_back(p);

        if (ready.empty()) {
            int next_arr = INT_MAX; const Process* nxt = nullptr;
            for (const auto &p : sorted) if (!done[p.pid]) {
                if (p.arrival < next_arr) { next_arr = p.arrival; nxt = &p; }
            }
            if (nxt && t < nxt->arrival) {
                tl.push_back({-1, t, nxt->arrival});
                t = nxt->arrival;
            }
            continue;
        }

        auto best = min_element(ready.begin(), ready.end(), [](const Process& x, const Process& y){
            if (x.priority != y.priority) return x.priority < y.priority; // smaller = higher
            if (x.arrival  != y.arrival)  return x.arrival  < y.arrival;
            return x.pid < y.pid;
        });
        tl.push_back({best->pid, t, t + best->burst});
        t += best->burst;
        done[best->pid] = true;
        finished++;
    }
    return finalizeMetrics("Priority (Non-Preemptive)", ps, tl);
}

static Result runRR(const vector<Process>& ps, int quantum) {
    if (quantum <= 0) quantum = 1; // safeguard
    int n = (int)ps.size();

    vector<Process> a = ps;
    sort(a.begin(), a.end(), [](const Process& x, const Process& y){
        if (x.arrival != y.arrival) return x.arrival < y.arrival;
        return x.pid < y.pid;
    });

    vector<int> rem(n+1, 0);
    for (auto &p : ps) rem[p.pid] = p.burst;

    vector<Segment> tl;
    queue<int> q; // PID queue

    int time = 0; size_t i = 0; int finished = 0;

    auto enqueueArrivals = [&](int upTo) {
        while (i < a.size() && a[i].arrival <= upTo) {
            q.push(a[i].pid); i++;
        }
    };

    // Initialize time to first arrival if needed
    if (!a.empty()) {
        if (time < a[0].arrival) {
            tl.push_back({-1, time, a[0].arrival});
            time = a[0].arrival;
        }
        enqueueArrivals(time);
    }

    vector<bool> inQueue(n+1, false);
    // We've already pushed arrivals at time 0..time
    {
        queue<int> tmp = q; while (!tmp.empty()) { inQueue[tmp.front()] = true; tmp.pop(); }
    }

    while (finished < n) {
        if (q.empty()) {
            // Jump to next arrival
            if (i < a.size()) {
                if (time < a[i].arrival) {
                    tl.push_back({-1, time, a[i].arrival});
                    time = a[i].arrival;
                }
                enqueueArrivals(time);
                // refresh inQueue flags
                queue<int> tmp = q; inQueue.assign(n+1, false);
                while (!tmp.empty()) { inQueue[tmp.front()] = true; tmp.pop(); }
                continue;
            } else {
                break; // no more processes (shouldn't happen without finishing all)
            }
        }

        int pid = q.front(); q.pop(); inQueue[pid] = false;
        if (rem[pid] == 0) continue; // already done (safety)

        int exec = min(quantum, rem[pid]);
        tl.push_back({pid, time, time + exec});
        time += exec;
        rem[pid] -= exec;

        // Enqueue any new arrivals up to 'time'
        enqueueArrivals(time);
        // Mark new inQueue flags
        queue<int> tmp = q; inQueue.assign(n+1, false);
        while (!tmp.empty()) { inQueue[tmp.front()] = true; tmp.pop(); }

        if (rem[pid] > 0) {
            // Put it back to the end of the queue
            q.push(pid); inQueue[pid] = true;
        } else {
            finished++;
        }
    }

    return finalizeMetrics("Round Robin (q=" + to_string(quantum) + ")", ps, tl);
}

// ---------- Data entry ----------

static vector<Process> enterProcesses() {
    int n = readInt("Enter number of processes (1..100): ", 1, 100);
    vector<Process> ps; ps.reserve(n);

    bool autoPID = true; // we assign PIDs as 1..n in order

    for (int i = 1; i <= n; ++i) {
        cout << "\n--- Enter data for Process P" << i << " ---\n";
        int arr = readInt("Arrival time (>=0): ", 0, 1'000'000);
        int burst = readInt("Burst time (>0): ", 1, 1'000'000);
        int prio = readInt("Priority (integer; smaller = higher): ", INT_MIN/2, INT_MAX/2);
        ps.push_back({i, arr, burst, prio});
    }

    // Optional: sort by PID (already is), return
    return ps;
}

static vector<Process> demoDataset() {
    // A small, mixed dataset with staggered arrivals
    // PID assigned 1..5 automatically
    vector<Process> ps = {
        {1, 0,  7, 3},
        {2, 2,  4, 1},
        {3, 4,  1, 4},
        {4, 5,  4, 2},
        {5, 6,  6, 5}
    };
    return ps;
}

// ---------- Comparison module ----------

static void compareAlgorithms(const vector<Process>& ps) {
    if (ps.empty()) { cout << "\n[Info] No processes to compare. Please enter data first.\n"; return; }

    Result r1 = runFCFS(ps);
    Result r2 = runSJF(ps);
    Result r3 = runPriorityNP(ps);

    int q = readInt("Enter time quantum for Round Robin (>0): ", 1, 1'000'000);
    Result r4 = runRR(ps, q);

    struct Row { string name; double aw; double at; };
    vector<Row> rows = {
        {r1.algo_name, r1.avg_wait, r1.avg_tat},
        {r2.algo_name, r2.avg_wait, r2.avg_tat},
        {r3.algo_name, r3.avg_wait, r3.avg_tat},
        {r4.algo_name, r4.avg_wait, r4.avg_tat}
    };

    sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){ return a.aw < b.aw; });

    cout << "\n=== Algorithm Comparison (lower is better) ===\n";
    cout << left << setw(28) << "Algorithm" << right << setw(18) << "Avg Waiting" << setw(22) << "Avg Turnaround" << "\n";
    cout << string(28+18+22, '-') << "\n";
    cout << fixed << setprecision(3);
    for (auto &rw : rows) {
        cout << left << setw(28) << rw.name << right << setw(18) << rw.aw << setw(22) << rw.at << "\n";
    }

    cout << "\nBest by Average Waiting Time: " << rows.front().name << "\n\n";
}

// ---------- Main menu ----------

static void runMenu() {
    vector<Process> processes;

    cout << "\nCPU Scheduling Algorithm Simulator and Evaluator\n";
    cout << "------------------------------------------------\n";

    if (readYesNo("Load a demo dataset to get started?", true)) {
        processes = demoDataset();
        printProcessTable(processes);
    }

    while (true) {
        cout << "\nMenu:\n";
        cout << " 1) Enter / Replace processes\n";
        cout << " 2) Show current processes\n";
        cout << " 3) Run FCFS\n";
        cout << " 4) Run SJF (Non-Preemptive)\n";
        cout << " 5) Run Round Robin\n";
        cout << " 6) Run Priority (Non-Preemptive)\n";
        cout << " 7) Compare All (with RR quantum)\n";
        cout << " 0) Exit\n";

        int choice = readInt("Choose an option: ", 0, 7);
        if (choice == 0) {
            cout << "Goodbye!\n"; break;
        }

        switch (choice) {
            case 1: {
                processes = enterProcesses();
                cout << "\n[Success] Process list updated.\n";
                break;
            }
            case 2: {
                if (processes.empty()) cout << "\n[Info] No processes loaded.\n";
                else printProcessTable(processes);
                break;
            }
            case 3: {
                if (processes.empty()) { cout << "\n[Info] No processes loaded.\n"; break; }
                Result r = runFCFS(processes);
                printResult(r, processes);
                break;
            }
            case 4: {
                if (processes.empty()) { cout << "\n[Info] No processes loaded.\n"; break; }
                Result r = runSJF(processes);
                printResult(r, processes);
                break;
            }
            case 5: {
                if (processes.empty()) { cout << "\n[Info] No processes loaded.\n"; break; }
                int q = readInt("Enter time quantum (>0): ", 1, 1'000'000);
                Result r = runRR(processes, q);
                printResult(r, processes);
                break;
            }
            case 6: {
                if (processes.empty()) { cout << "\n[Info] No processes loaded.\n"; break; }
                Result r = runPriorityNP(processes);
                printResult(r, processes);
                break;
            }
            case 7: {
                compareAlgorithms(processes);
                break;
            }
        }
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    try {
        runMenu();
    } catch (const exception &e) {
        cerr << "\n[Fatal Error] " << e.what() << "\n";
        return 1;
    }
    return 0;
}
