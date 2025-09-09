//concurrent.cpp

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;

struct Record {
    uint64_t minuteIdx;  // index of 5-minute slots from start (0,1,2,...)
    string light; // format: "L001"
    int cars; // cars==-1 => poison pill
};

static inline uint64_t hourKeyFromMinute(uint64_t minuteIdx, uint32_t minutesPerHour=60, uint32_t step=5){
    // Convert 5-minute index into hour bucket
    return (minuteIdx * step) / minutesPerHour;
}

Record parseLine(const string& s){
    stringstream ss(s);
    string a,b,c;
    getline(ss, a, ',');
    getline(ss, b, ',');
    getline(ss, c, ',');
    return Record{stoull(a), b, stoi(c)};
}

class BoundedQueue {
    vector<Record> buf;
    size_t head=0, tail=0, count=0;
    mutex m;
    condition_variable cvNotEmpty, cvNotFull;
public:
    explicit BoundedQueue(size_t cap): buf(max<size_t>(cap,1)) {}
    void push(const Record& r){
        unique_lock<mutex> lk(m);
        cvNotFull.wait(lk, [&]{ return count < buf.size(); });
        buf[tail] = r;
        tail = (tail + 1) % buf.size();
        ++count;
        cvNotEmpty.notify_one();
    }
    Record pop(){
        unique_lock<mutex> lk(m);
        cvNotEmpty.wait(lk, [&]{ return count > 0; });
        Record r = buf[head];
        head = (head + 1) % buf.size();
        --count;
        cvNotFull.notify_one();
        return r;
    }
};

int main(int argc, char** argv){
    if(argc < 7){
        cerr << "Usage: ./conc <input.csv> <topN> <producers> <consumers> <capacity> <stepMinutes>\n";
        return 1;
    }
    string path = argv[1];
    int topN = stoi(argv[2]);
    int P = stoi(argv[3]), C = stoi(argv[4]);
    size_t CAP = stoul(argv[5]);
    int STEP = stoi(argv[6]);

    // Load file into memory 
    vector<string> lines;
    {
        ifstream in(path);
        if(!in){ cerr << "Cannot open " << path << "\n"; return 1; }
        string line;
        while(getline(in, line)) if(!line.empty()) lines.push_back(line);
    }


    BoundedQueue q(CAP);

     // Shared aggregator: hour -> (light -> sum)
    unordered_map<uint64_t, unordered_map<string,int>> totals; 
    mutex totals_m;

    atomic<size_t> nextIdx{0};

    auto producer = [&](){
        for(;;){
            size_t i = nextIdx.fetch_add(1, memory_order_relaxed);
            if(i >= lines.size()) break;
            Record r = parseLine(lines[i]);
            q.push(r);
        }
    };

    auto consumer = [&](){
        unordered_map<uint64_t, unordered_map<string,int>> local;
        size_t batch = 0;
        for(;;){
            Record r = q.pop();
            if(r.cars == -1) break; // poison pill
            uint64_t h = hourKeyFromMinute(r.minuteIdx, 60, STEP);
            local[h][r.light] += r.cars;

            if(++batch % 2048 == 0){
                lock_guard<mutex> lk(totals_m);
                for(auto& [h2, mp] : local){
                    auto& tgt = totals[h2];
                    for(auto& [light, sum] : mp) tgt[light] += sum;
                }
                local.clear();
            }
        }
        if(!local.empty()){
            lock_guard<mutex> lk(totals_m);
            for(auto& [h2, mp] : local){
                auto& tgt = totals[h2];
                for(auto& [light, sum] : mp) tgt[light] += sum;
            }
        }
    };

    vector<thread> prod, cons;
    prod.reserve(P); cons.reserve(C);
    for(int i=0;i<P;i++) prod.emplace_back(producer);
    for(int i=0;i<C;i++) cons.emplace_back(consumer);

    for(auto& t: prod) t.join();

    // send poison pills per consumer
    for(int i=0;i<C;i++) q.push(Record{0,"",-1});
    for(auto& t: cons) t.join();

    // Deterministic output
    vector<uint64_t> hours;
    hours.reserve(totals.size());
    for(auto& kv: totals) hours.push_back(kv.first);
    sort(hours.begin(), hours.end());

    for(uint64_t hour : hours){
        auto& mp = totals[hour];
        using Pair = pair<int,string>;
        vector<Pair> v;
        v.reserve(mp.size());
        for(auto& kv : mp) v.push_back({kv.second, kv.first});
        sort(v.begin(), v.end(), [](const Pair& a, const Pair& b){
            if(a.first!=b.first) return a.first>b.first;
            return a.second<b.second;
        });
        cout << "Hour " << hour << " top " << topN << ":\n";
        for(int i=0;i<topN && i<(int)v.size();++i){
            cout << "  " << v[i].second << " -> " << v[i].first << "\n";
        }
    }
    return 0;
}


// gen.cpp

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <random>

using namespace std;

int main(int argc, char** argv){
    if(argc<6){
        cerr << "Usage: ./gen <hours> <lights> <stepMin> <seed> <out.csv>\n";
        return 1;
    }
    int hours = stoi(argv[1]);
    int L = stoi(argv[2]);
    int step = stoi(argv[3]); // e.g., 5
    int seed = stoi(argv[4]);
    string out = argv[5];

    mt19937 rng(seed);
    uniform_int_distribution<int> base(0, 10), spike(0, 100);

    ofstream f(out);
    if(!f){ cerr << "Cannot open " << out << "\n"; return 1; }

    int idx=0; // 5-minute slot index
    for(int h=0; h<hours; ++h){
        for(int m=0; m<60; m+=step){
            for(int li=0; li<L; ++li){
                int cars = base(rng);
                if(uniform_int_distribution<int>(0,20)(rng)==0) cars += spike(rng);
                f << idx << ",L" << setw(3) << setfill('0') << li << "," << cars << "\n";
            }
            ++idx;
        }
    }
    return 0;
}


// sequential.cpp
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace std;

struct Record {
    uint64_t minuteIdx;
    string light;
    int cars;
};

static inline uint64_t hourKeyFromMinute(uint64_t minuteIdx, uint32_t minutesPerHour=60, uint32_t step=5){
    return (minuteIdx * step) / minutesPerHour;
}

Record parseLine(const string& s) {
    stringstream ss(s);
    string a,b,c;
    getline(ss, a, ',');
    getline(ss, b, ',');
    getline(ss, c, ',');
    return Record{stoull(a), b, stoi(c)};
}

int main(int argc, char** argv){
    if(argc < 3){
        cerr << "Usage: ./seq <input.csv> <topN>\n";
        return 1;
    }
    string path = argv[1];
    int topN = stoi(argv[2]);

    ifstream in(path);
    if(!in){ cerr << "Cannot open " << path << "\n"; return 1; }

    unordered_map<uint64_t, unordered_map<string,int>> totals; // hour -> (light -> sum)
    string line;
    while(getline(in, line)){
        if(line.empty()) continue;
        Record r = parseLine(line);
        uint64_t h = hourKeyFromMinute(r.minuteIdx);
        totals[h][r.light] += r.cars;
    }

    // Deterministic printing
    vector<uint64_t> hours;
    hours.reserve(totals.size());
    for(auto& kv: totals) hours.push_back(kv.first);
    sort(hours.begin(), hours.end());

    for(uint64_t hour : hours){
        auto& mp = totals[hour];
        using Pair = pair<int,string>; // (sum, light)
        vector<Pair> v;
        v.reserve(mp.size());
        for(auto& kv : mp) v.push_back({kv.second, kv.first});
        // Top-N by sum desc, then light asc
        sort(v.begin(), v.end(), [](const Pair& a, const Pair& b){
            if(a.first!=b.first) return a.first>b.first;
            return a.second<b.second;
        });
        cout << "Hour " << hour << " top " << topN << ":\n";
        for(int i=0;i<topN && i<(int)v.size();++i){
            cout << "  " << v[i].second << " -> " << v[i].first << "\n";
        }
    }
    return 0;
}

