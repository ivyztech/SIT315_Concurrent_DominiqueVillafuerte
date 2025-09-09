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
